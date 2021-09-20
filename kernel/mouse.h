#ifndef MOUSE_H
#define MOUSE_H

#include "types.h"
#include "x86.h"
#include "defs.h"
#include "traps.h"
#include "spinlock.h"
#include "sleeplock.h"

typedef struct {
/*
  uchar y_overflow : 1; // not used
  uchar x_overflow : 1; // not used
  uchar y_sign : 1;
  uchar x_sign : 1;
  uchar always1 : 1;    // Bit 3, always 1
  uchar middle_btn : 1;
  uchar right_btn : 1;
  uchar left_btn : 1;
*/

// reversed order (IMPORTANT!!!)
  uchar left_btn : 1;
  uchar right_btn : 1;
  uchar middle_btn : 1;
  uchar always1 : 1;    // Bit 3, always 1
  uchar x_sign : 1;
  uchar y_sign : 1;
  uchar x_overflow : 1; // not used
  uchar y_overflow : 1; // not used
} mflags_t;

typedef struct {
  uchar flags;        // byte 1
  uchar x_movement;   // byte 2
  uchar y_movement;   // byte 3
} mpkt_t;


#endif
