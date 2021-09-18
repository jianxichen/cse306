#include "types.h"
#include "x86.h"
#include "defs.h"
#include "kbd.h"

void mouseinit(){
    // Check and wait till controller is ready
    while((inb(KBSTATP)&0x2)==0){
        cprintf("PS2 controller status not ready");
    }
    // Issue compaq byte to controller to activate IRQ12
    outb(KBSTATP, 0x20);

    // Read status byte back
    uchar st=inb(KBSTATP);

    // Modify status byte
    st=st|0x2;
    st=st&0x20;

    // Issue compaq byte and modified databyte
    outb(KBSTATP, 0x20);
    outb(KBDATAP, st);
}

void mouseintr(){
    
}