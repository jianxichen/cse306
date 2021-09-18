#include "types.h"
#include "x86.h"
#include "defs.h"
#include "kbd.h"

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

void
kbdintr(void)
{
  uint st = inb(KBSTATP);

  if((st & KBS_DIB) == 0)  // before reading data from 0x60, verify that bit 0 (value = 0x1) is set.
    return;
  while(st & KBS_DIB) {
    data_buf = inb(KBDATAP);
    cprintf("st = %d  ", st);  // st = 29 = 0x1D = 0001 1101
    if(st & 0x20){  // bit 5 is set ==> mouse
      cprintf("mouse\n");
    } else {  // bit 5 is clear ==> keyboard
      cprintf("keyboard\n");
    }
    st = inb(KBSTATP);
  }

  consoleintr(kbdgetc);
}