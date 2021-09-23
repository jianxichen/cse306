#include "types.h"
#include "x86.h"
#include "defs.h"
#include "kbd.h"

static int PSTAT = 0x64; // REMOVE THESE ONCES WAIT_READ GETS TOO MUCH
// static int PDATA = 0x60; // REMOVE THESE ONCES WAIT_READ GETS TOO MUCH
static int BIT0 = 0x01; // REMOVE THESE ONCES WAIT_READ GETS TOO MUCH
// static int BIT1 = 0x02; // REMOVE THESE ONCES WAIT_READ GETS TOO MUCH
// static int BIT3 = 0x08; // REMOVE THESE ONCES WAIT_READ GETS TOO MUCH
// static int BIT5 = 0x20; // REMOVE THESE ONCES WAIT_READ GETS TOO MUCH

#ifndef ISSET // REMOVE THESE ONCES WAIT_READ GETS TOO MUCH
  #define ISSET
  #define IS_SET(stat, bit) ((stat) & (bit))
#endif
#ifndef ISCLEAR // REMOVE THESE ONCES WAIT_READ GETS TOO MUCH
  #define ISCLEAR
  #define IS_CLEAR(stat, bit) (!((stat) & (bit)))
#endif

volatile uint data_buf = 0;  // May be a char array??? Now it seems working, somehow.

int
kbdgetc(void)
{
  static uint shift;
  static uchar *charcode[4] = {
    normalmap, shiftmap, ctlmap, ctlmap
  };
  uint c;
  
  if (data_buf == 0) {
    return -1; // no buf
  }

  if(data_buf == 0xE0){
    shift |= E0ESC;
    data_buf = 0;
    return 0;
  } else if(data_buf & 0x80){
    // Key released
    data_buf = (shift & E0ESC ? data_buf : data_buf & 0x7F);
    shift &= ~(shiftcode[data_buf] | E0ESC);
    data_buf = 0;
    return 0;
  } else if(shift & E0ESC){
    // Last character was an E0 escape; or with 0x80
    data_buf |= 0x80;
    shift &= ~E0ESC;
  }

  shift |= shiftcode[data_buf];
  shift ^= togglecode[data_buf];
  c = charcode[shift & (CTL | SHIFT)][data_buf];
  if(shift & CAPSLOCK){
    if('a' <= c && c <= 'z')
      c += 'A' - 'a';
    else if('A' <= c && c <= 'Z')
      c += 'a' - 'A';
  }
  data_buf = 0;
  return c;
}

// REMOVE THESE ONCES WAIT_READ GETS TOO MUCH
static void wait_read()
{
  uint timeout = 10000;
  uchar status = 0;
  while (--timeout) {
    status = inb(PSTAT);
    if (IS_SET(status, BIT0)) {
      // cprintf("Readable\n");
      return;
    }
  }
  cprintf("wait for reading - timeout\n");
} 

// // REMOVE THESE ONCES WAIT_READ GETS TOO MUCH
// static void wait_write()
// {
//   uint timeout = 10000;
//   uchar status = 0;
//   while (--timeout) {
//     status = inb(PSTAT);
//     if (IS_CLEAR(status, BIT1)) {
//       // cprintf("Writable\n");
//       return;
//     }
//   }
//   cprintf("wait for writing - timeout\n");
// }

void
kbdintr(void)
{
  wait_read();
  uint st = inb(KBSTATP);

  if((st & KBS_DIB) == 0)  // before reading data from 0x60, verify that bit 0 (value = 0x1) is set.
    return;
  while(st & KBS_DIB) {
    data_buf = inb(KBDATAP);
    // cprintf("st = %d  ", st);  // st = 29 = 0x1D = 0001 1101
    if(st & 0x20){  // bit 5 is set ==> mouse
      // cprintf("mouse event\n");
      data_buf = 0;
    } else {  // bit 5 is clear ==> keyboard
      // cprintf("keyboard event\n");
      break;
    }
    wait_read();
    st = inb(KBSTATP);
  }

  consoleintr(kbdgetc);
}
