#include "kernel/types.h"
#include "user.h"

void handler(int sig){
    printf(0, "i have received sig %d\n", sig);
}

int main(){
    int currentpid=getpid();
    handler(2);
    int addr=(int)&handler;
    printf(0, "my pid is: %d\n", currentpid);
    printf(0, "handler address is %d\n", addr);
    sigsethandler(500, handler);
    int blockmask=~(0x1);
    sigpause(blockmask);
    printf(0, "ending\n");
}