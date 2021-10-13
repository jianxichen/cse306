#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

int sys_sigsend(){
  int pid, sig;
  argint(0, &pid);
  argint(1, &sig);
  return sigsend(pid, sig);
}

int sys_sigsethandler(){
  int sig;
  void *func;
  argint(0, &sig);
  argptr(1, (char**) &func, 4);
  sigsethandler(sig, func);
  return 0;
}

int sys_sigreturn(){
  sigreturn();
  return 0;
}

int sys_siggetmask(){
  return siggetmask();
}

int sys_sigsetmask(){
  int *maskp;
  argptr(0, (char**)&(maskp), sizeof(*maskp));
  return sigsetmask(maskp);
}

int sys_sigpause(){
  return 0;
}