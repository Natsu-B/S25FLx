/*
Arduino S25FLx Serial Flash library
By John-Mike Reed (Dr. Bleep) of Bleep labs
bleeplabs.com

Usage guide at github.com/BleepLabs/S25FLx/
Datasheet for S25FL216K www.mouser.com/ds/2/380/S25FL216K_00-6756.pdf
This library can interface with most of the S25FL family with no modifications.

This free library is realeased under Creative comoms license CC BY-SA 3.0
http://creativecommons.org/licenses/by-sa/3.0/deed.en_US
*/

// #include "arduino.h"
#include <Arduino.h>
#include <SPI.h>
#include "S25FLx.h"

// Define S25FLx control bytes

#define WREN 0x06      /* Write Enable */
#define WRDI 0x04      /* Write Disable */
#define RDSR 0x05      /* Read Status Register */
#define WRSR 0x01      /* Write Status Register */
#define READ 0x03      /* Read Data Bytes  */
#define FAST_READ 0x0b /* Read Data Bytes at Higher Speed //Not used as as the 328 isn't fast enough  */
#define PP 0x02        /* Page Program  */
#define SE 0x20        /* Sector Erase (4k)  */
#define BE 0x20        /* Block Erase (64k)  */
#define CE 0xc7        /* Erase entire chip  */
#define DP 0xb9        /* Deep Power-down  */
#define RES 0xab       /* Release Power-down, return Device ID */
#define RDID 0x9F      /* Read Manufacture ID, memory type ID, capacity ID */

#define cs 16 // Chip select pin
unsigned long prev;

// A great little tool for printing a byte as binary without it chopping off the leading zeros.
// from http://forum.arduino.cc/index.php/topic,46320.0.html

#define DEBUG

template <class... Args>
void pr_debug(Args... words)
{
#ifdef DEBUG
  Serial.print(words...);
#endif
}

template <class... Args>
void prln_debug(Args... words)
{
#ifdef DEBUG
  Serial.println(words...);
#endif
}

flash::flash()
{
  SPI.begin();
  SPI.setBitOrder(MSBFIRST);
  SPI.setClockDivider(SPI_CLOCK_DIV8);
  SPI.setDataMode(SPI_MODE0);
}

void printBits(byte myByte)
{
  for (byte mask = 0x80; mask; mask >>= 1)
  {
    if (mask & myByte)
      pr_debug('1');
    else
      pr_debug('0');
  }
}

// read and return the status register.
byte flash::stat()
{ // check status register
  // digitalWriteFast
  digitalWrite(cs, LOW);
  SPI.transfer(RDSR);
  byte s = SPI.transfer(0);
  // digitalWriteFast
  digitalWrite(cs, HIGH);
  //  printBits(s);
  return s;
}

// use between each communication to make sure S25FLxx is ready to go.
void flash::waitforit()
{
  byte s = stat();
  while ((s & B0000001) == B00000001)
  { // check if WIP bit is 1
    //  while (s==B00000011||s==B00000001){
    if ((millis() - prev) > 1000)
    {
      prev = millis();
      pr_debug("S25FL Busy. Status register = B");
      printBits(s);
      prln_debug();
    }

    s = stat();
  }
}

// Must be done to allow erasing or writing
void flash::write_enable()
{
  // digitalWriteFast
  digitalWrite(cs, LOW);
  SPI.transfer(WREN);
  // digitalWriteFast
  digitalWrite(cs, HIGH);
  waitforit();
  // prln_debug("write enabled");
}

// Erase an entire 4k sector the location is in.
// For example "erase_4k(300);" will erase everything from 0-3999.
//
// All erase commands take time. No other actions can be preformed
// while the chip is errasing except for reading the register
void flash::erase_4k(unsigned long loc)
{

  waitforit();
  write_enable();

  // digitalWriteFast
  digitalWrite(cs, LOW);
  SPI.transfer(0x20);
  SPI.transfer(loc >> 16);
  SPI.transfer(loc >> 8);
  SPI.transfer(loc & 0xFF);
  // digitalWriteFast
  digitalWrite(cs, HIGH);
  waitforit();
}

// Errase an entire 64_k sector the location is in.
// For example erase4k(530000) will erase everything from 524543 to 589823.

void flash::erase_64k(unsigned long loc)
{

  waitforit();
  write_enable();

  // digitalWriteFast
  digitalWrite(cs, LOW);
  SPI.transfer(0x20);
  SPI.transfer(loc >> 16);
  SPI.transfer(loc >> 8);
  SPI.transfer(loc & 0xFF);
  // digitalWriteFast
  digitalWrite(cs, HIGH);
  waitforit();
}

// errases all the memory. Can take several seconds.
void flash::erase_all()
{
  waitforit();
  write_enable();
  // digitalWriteFast
  digitalWrite(cs, LOW);
  SPI.transfer(CE);
  // digitalWriteFast
  digitalWrite(cs, HIGH);
  waitforit();
}

// Read data from the flash chip. There is no limit "length". The entire memory can be read with one command.
// read_S25(starting location, array, number of bytes);
void flash::read(unsigned long loc, uint8_t *array, unsigned long length)
{
  // digitalWriteFast
  digitalWrite(cs, LOW);
  SPI.transfer(READ);      // control byte follow by location bytes
  SPI.transfer(loc >> 16); // convert the location integer to 3 bytes
  SPI.transfer(loc >> 8);
  SPI.transfer(loc & 0xff);

  for (int i = 0; i < length + 1; i++)
  {
    array[i] = SPI.transfer(0); // send the data
  }
  // digitalWriteFast
  digitalWrite(cs, HIGH);
}

// Programs up to 256 bytes of data to flash chip. Data must be erased first. You cannot overwrite.
// Only one continuous page (256 Bytes) can be programmed at once so there's some
// sorcery going on here to make it not wrap around.
// It's most efficent to only program one page so if you're going for speed make sure your
// location %=0 (for example location=256, length=255.) or your length is less that the bytes remain
// in the page (location =120 , length= 135)

// write_S25(starting location, array, number of bytes);
void flash::write(unsigned long loc, uint8_t *array, unsigned long length)
{

  if (length > 255)
  {
    unsigned long reps = length >> 8;
    unsigned long length1;
    unsigned long array_count;
    unsigned long first_length;
    unsigned remainer0 = length - (256 * reps);
    unsigned long locb = loc;

    pr_debug("reps ");
    prln_debug(reps);
    pr_debug("remainer0 ");
    prln_debug(remainer0);

    for (int i = 0; i < (reps + 2); i++)
    {

      if (i == 0)
      {

        length1 = 256 - (locb & 0xff);
        first_length = length1;
        if (length1 == 0)
        {
          i++;
        }
        array_count = 0;
      }

      if (i > 0 && i < (reps + 1))
      {
        locb = first_length + loc + (256 * (i - 1));
        ;

        array_count = first_length + (256 * (i - 1));
        length1 = 255;
      }
      if (i == (reps + 1))
      {
        locb += (256);
        array_count += 256;
        length1 = remainer0;
        if (remainer0 == 0)
        {
          break;
        }
      }
      // pr_debug("i ");prln_debug(i);
      // pr_debug("locb ");prln_debug(locb);
      // pr_debug("length1 ");prln_debug(length1);
      // pr_debug("array_count ");prln_debug(array_count );

      write_enable();
      waitforit();
      // digitalWriteFast
      digitalWrite(cs, LOW);
      SPI.transfer(PP);
      SPI.transfer(locb >> 16);
      SPI.transfer(locb >> 8);
      SPI.transfer(locb & 0xff);

      for (unsigned long i = array_count; i < (length1 + array_count + 1); i++)
      {
        SPI.transfer(array[i]);
      }

      // digitalWriteFast
      digitalWrite(cs, HIGH);
      waitforit();

      // prln_debug("//////////");
    }
  }

  if (length <= 255)
  {
    if (((loc & 0xff) != 0) | ((loc & 0xff) < length))
    {
      byte remainer = loc & 0xff;
      byte length1 = 256 - remainer;
      byte length2 = length - length1;
      unsigned long page1_loc = loc;
      unsigned long page2_loc = loc + length1;

      write_enable();
      waitforit();
      // digitalWriteFast
      digitalWrite(cs, LOW);
      SPI.transfer(PP);
      SPI.transfer(page1_loc >> 16);
      SPI.transfer(page1_loc >> 8);
      SPI.transfer(page1_loc & 0xff);

      for (int i = 0; i < length1; i++)
      {
        SPI.transfer(array[i]);
      }

      // digitalWriteFast
      digitalWrite(cs, HIGH);
      waitforit();
      write_enable();

      waitforit();

      // digitalWriteFast
      digitalWrite(cs, LOW);
      SPI.transfer(PP);
      SPI.transfer(page2_loc >> 16);
      SPI.transfer(page2_loc >> 8);
      SPI.transfer(page2_loc & 0xff);

      for (int i = length1; i < length + 1; i++)
      {
        SPI.transfer(array[i]);
      }

      // digitalWriteFast
      digitalWrite(cs, HIGH);
      waitforit();
      // prln_debug("//////////");
      // pr_debug("remainer ");prln_debug(remainer);

      // pr_debug("length1 ");prln_debug(length1);
      // pr_debug("length2 ");prln_debug(length2);
      // pr_debug("page1_loc ");prln_debug(page1_loc);
      // pr_debug("page2_loc ");prln_debug(page2_loc);
      // prln_debug("//////////");
    }

    else
    {
      pr_debug("loc & 0xff = ");
      prln_debug(loc & 0xff);

      write_enable(); // Must be done before writing can commence. Erase clears it.
      waitforit();
      // digitalWriteFast
      digitalWrite(cs, LOW);
      SPI.transfer(PP);
      SPI.transfer(loc >> 16);
      SPI.transfer(loc >> 8);
      SPI.transfer(loc & 0xff);

      for (int i = 0; i < length + 1; i++)
      {
        SPI.transfer(array[i]);
      }

      // digitalWriteFast
      digitalWrite(cs, HIGH);
      waitforit();
    }
  }
}

// Used in conjuture with the write protect pin to protect blocks.
// For example on the S25FL216K sending "write_reg(B00001000);" will protect 2 blocks, 30 and 31.
// See the datasheet for more. http://www.mouser.com/ds/2/380/S25FL216K_00-6756.pdf
void flash::write_reg(byte w)
{
  // digitalWriteFast
  digitalWrite(cs, LOW);
  SPI.transfer(WRSR);
  SPI.transfer(w);
  // digitalWriteFast
  digitalWrite(cs, HIGH);
}

void flash::read_info()
{
  // digitalWriteFast
  digitalWrite(cs, LOW);
  SPI.transfer(0x9F);
  //  SPI.transfer(0);
  byte m = SPI.transfer(0);
  byte t = SPI.transfer(0);
  byte c = SPI.transfer(0);

  // digitalWriteFast
  digitalWrite(cs, HIGH);

  while (c == 0)
  {
    prln_debug("Cannot read S25FL. Check wiring");
  }

  pr_debug("Manufacturer ID: ");
  pr_debug(m);
  pr_debug("     Memory type: ");
  pr_debug(t);
  pr_debug("     Capacity: ");
  prln_debug(c);
  prln_debug();
  waitforit();
}
