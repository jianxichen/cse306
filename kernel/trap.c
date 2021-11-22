#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  // cprintf("\n%d\n", tf->trapno);
  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    myproc()->kernelmode=1; // set my proc flag to kernel-mode, syscall : hw 3 step 6
    syscall();
    // set my proc flag back to user-mode : hw 3 step 6
    myproc()->kernelmode=0;

    // Serve a process' signal b/c syscall
    uint ignored=0, shift=0;
    while(myproc()->pendingsig & ~(myproc()->blockedsig)){ // see if there exists unmasked pending signals
      uint signal=1<<shift;
      if((myproc()->pendingsig)&(signal) && (((myproc()->blockedsig)&(signal))==0)){ // isolate for specific signals
        if((void*)(myproc()->func[shift])>(void*)0){ // check if there exists a user handle for it
          if(ignored==shift){ // check if signal is ignored
            continue;
          }else{
            // create signal stackframe on user stack
            // 1) save current trapframe on user stack
            struct trapframe currtf=*(myproc()->tf);
            myproc()->tf->esp-=sizeof(struct trapframe);
            *(struct trapframe*)(myproc()->tf->esp)=currtf;
            // 2) signum pushed onto user stack
            myproc()->tf->esp-=4;
            *(int*)(myproc()->tf->esp)=shift;
            // 3) push trampoline address to user stack
            myproc()->tf->esp-=4;
            *(int*)(myproc()->tf->esp)=(int)(myproc()->tramp);
            // 4) replace trap frame eip with sighandler
            myproc()->tf->eip=(uint)(myproc()->func[shift]);
            // 5) signal being handled is added to mask
            ignored=(signal==1) ? ignored : ignored|shift; // update ignore mask
            myproc()->blockedsig|=signal;
          }
          acquire(&(myproc()->pendwrite));
          myproc()->pendingsig&=~shift;
          release(&(myproc()->pendwrite));
        }else{
          kill(myproc()->pid); // pending signal's default action is to be killed
          break;
        }
      }
      shift=(shift+1)%32;
    }

    if(myproc()->killed){
      exit();
    }
    return;
  }

  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE2: // Original: IDQ_IDE+1
    // Bochs generates spurious IDE1 interrupts.
    // cprintf("ide2 interrupt\n");
    ide2intr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_MOUSE:
    mouseintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
  case T_IRQ0 + IRQ_SPURIOUS1:
    cprintf("cpu%d: spurious interrupt (%d) at %x:%x\n",
            cpuid(), tf->trapno, tf->cs, tf->eip);
    lapiceoi();
    break;

  //PAGEBREAK: 13
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      //panic("trap");
      return;
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Invoke the scheduler on clock tick.
  if(tf->trapno == T_IRQ0+IRQ_TIMER)
    reschedule();

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}
