#include "kernel/types.h"
#include "user.h"

void handler(int sig){
    printf(1, "i have received sig %d\n", sig);
}

int main(){
    int currentpid=getpid();
    handler(2);
    int addr=(int)&handler;
    printf(1, "my pid is: %d\n", currentpid);
    printf(1, "handler address is %d\n", addr);
    sigsethandler(500, handler);
    int blockmask=~(0x1);
    sigpause(blockmask);
    printf(1, "ending\n");
}