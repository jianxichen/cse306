#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "semaphore.h"

void sem_init(struct semaphore *sp, int val){
    sp->value=val;
    initlock(&(sp->chan), "sema");
}

void sem_P(struct semaphore *sp){
    acquire(&(sp->chan));
    if(sp->value==0){
        sleep(&(sp->chan),&(sp->chan));
    }
    sp->value--;
    release(&(sp->chan));
}

void sem_V(struct semaphore *sp){
    acquire(&(sp->chan));
    sp->value++;
    release(&(sp->chan));
    wakeup(&(sp->chan));
}