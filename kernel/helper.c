#include "helper.h"
#include "types.h"
#include "defs.h"

int isKeyboard(int c){
    return (c & 0x20) == 0;
}

void printTick(void){
    while(ticks<0xffff){
        if(ticks%100==0){
            cprintf("100 ticks");
        }
    }
}