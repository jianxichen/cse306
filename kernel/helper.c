#include "helper.h"

int isKeyboard(int c){
    return (c & 0x20) == 0;
}