#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "helper.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

uint loadavg=0; // load_avg = p * current_load + (1-p) * load_avg
int runnables=0; // runnables and loadavg updated every tick timer

static struct proc *initproc;
static int newproc=0; // new proc flag step 1 hw 3

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);
static void sched(void);
static struct proc *roundrobin(void);
static struct proc *shortestprocessnext(void); // step 6 hw 3
static struct proc *shortestremainingtime(void);
static struct proc *highestresponseratio(void);
static struct proc *(*policy[])(void)={
  [0]       roundrobin,
  [1]       shortestprocessnext,
  [2]       shortestremainingtime,
  [3]       highestresponseratio
};
static void adjustallpticks(int ind);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED){
      p->pendingsig=p->blockedsig=(uint)0;
      initsleeplock(&(p->pendsleep), "pendingsleep");
      initlock(&(p->pendwrite), "pendingwrite");
      for(int i=0; i<32; i++){
        p->func[i]=(void (*) (int)) -1;
      }
      p->tramp=sigreturn_bounce;
      newproc=1;
      p->tick=(struct ptimes) {0};
      p->tick.pt_real=ticks;
      p->eticks=0;
      goto found;
    }

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // // Copy process state from proc.
  // if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
  //   kfree(np->kstack);
  //   np->kstack = 0;
  //   np->state = UNUSED;
  //   return -1;
  // }

  // Reference copy of curproc's Pd
  np->pgdir=curproc->pgdir;
  // Iterate all encompassing Pt entries in Pdirectory
  pte_t *pte;
  for(int i=0; i<curproc->sz; i+=PGSIZE){
    if((pte=walkpgdir(curproc->pgdir, (void*)i, 0))==0){
      panic("forklazy should exist pte but doesn't");
    }
    *pte&=~PTE_W;
    // get PPN in pte
    chgpgrefc(P2V((void*)(*pte)), 1);
  }
  lcr3(V2P(curproc->pgdir));

  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  np->kernelmode=0; // hw 3 step 6

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(struct ptimes *ptime)
{
  struct proc *curproc = myproc();

  struct proc *p;
  int havekids, pid;
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        // step 3 hw3 (assume its a pointer pass rather copying over the struct)
        if(ptime){
          ptime=&(p->tick);
        }
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU idle loop.
// Each CPU calls idle() after setting itself up.
// Idle never returns.  It loops, executing a HLT instruction in each
// iteration.  The HLT instruction waits for an interrupt (such as a
// timer interrupt) to occur.  Actual work gets done by the CPU when
// the scheduler is invoked to switch the CPU from the idle loop to
// a process context.
void
idle(void)
{
  // cprintf("entered idle!\n");
  sti(); // Enable interrupts on this processor
  for(;;) {
    if(!(readeflags()&FL_IF)) // if (hardware interrupts is blocked)
      panic("idle non-interruptible");
    
    // step 1 hw3 (working?)
    acquire(&(ptable.lock));
    int found=newproc; // check new proc flag
    newproc=0;
    release(&(ptable.lock));
    if(found){
      // pushcli();
      cli();
      // cprintf("from idle!\n");
      reschedule();
      // popcli();
      sti();
    }else{
      hlt(); // Wait for an interrupt
    }

    // hlt();
  }
}

// The process scheduler.
//
// Assumes ptable.lock is held, and no other locks.
// Assumes interrupts are disabled on this CPU.
// Assumes proc->state != RUNNING (a process must have changed its
// state before calling the scheduler).
// Saves and restores intena because the original xv6 code did.
// (Original comment:  Saves and restores intena because intena is
// a property of this kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would break in the few
// places where a lock is held but there's no process.)
//
// When invoked, does the following:
//  - choose a process to run
//  - swtch to start running that process (or idle, if none)
//  - eventually that process transfers control
//      via swtch back to the scheduler.

static void
sched(void)
{
  int intena;
  struct proc *p;
  struct context **oldcontext;
  struct cpu *c = mycpu();
  
  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(c->ncli != 1)
    panic("sched locks");
  if(readeflags()&FL_IF)
    panic("sched interruptible");

  // set reset runnables to 0 and count how many processes are competing for CPU / in RUNNABLE
  runnables=0;
  for(int i=0; i<NPROC; i++){
    if(ptable.proc[i].state==RUNNABLE||ptable.proc[i].state==RUNNING){
      runnables++;
    }
  }
  // loadavg=0.9992328f*loadavg;
  // loadavg=loadavg+0.0007672f*runnables;
  loadavg=(9992328*loadavg)/10000000; // get 4 decimal digit ==> value ABCD=0.ABCD
  loadavg=loadavg+(7672*runnables*10000)/10000000; // get 4 decimal digit ==> value ABCD=0.ABCD
// float x = 25.83f*3;
// int y = (int)(x + 0.5f);
// cprintf(" this is y %d", y); // this works
// int loadavgint=(int)(loadavg*10000);
// cprintf("this is loadavgint %d", loadavgint); // this doesnt
// y=y; loadavgint=loadavgint;

  // Determine the current context, which is what we are switching from.
  if(c->proc) {
    if(c->proc->state == RUNNING)
      panic("sched running");
    oldcontext = &c->proc->context;
  } else {
    oldcontext = &(c->scheduler);
  }

  // Roundrobin on kernel-mode process first hw 3 step 6
  // pretty sure there exists a flag/register to see if CPU is in kernel-mode
  // or not but forgot/unsure of what it is
  intena=0; // intena used as "found kernel-mode process" flag
  for(int i = 0; i < NPROC; i++) {
    p = &(ptable.proc[i]);
    if((p->state) != RUNNABLE || (p->kernelmode==0)){
      continue;
    }
    if(intena==0){
      adjustallpticks(p - ptable.proc); // hw 3 step 2|6 : adjust ticks
      intena=1;
      break;
    }
  }
  if(intena){ // hw 3 step 6 : choose first kernel-mode process over user
    // Switch to chosen process.  It is the process's job
    // to release ptable.lock and then reacquire it
    // before jumping back to us.
    p->state = RUNNING;
    switchuvm(p);
    if(c->proc != p) {
      c->proc = p;
      intena = c->intena;
      swtch(oldcontext, p->context);
      mycpu()->intena = intena;  // We might return on a different CPU.
    }
  }else 
  // Choose next process to run.
  if((p = policy[POLICY]()) != 0) {
    // Switch to chosen process.  It is the process's job
    // to release ptable.lock and then reacquire it
    // before jumping back to us.
    p->state = RUNNING;
    switchuvm(p);
    if(c->proc != p) {
      c->proc = p;
      intena = c->intena;
      swtch(oldcontext, p->context);
      mycpu()->intena = intena;  // We might return on a different CPU.
    }
  } else {
    // No process to run -- switch to the idle loop.
    switchkvm();
    if(oldcontext != &(c->scheduler)) {
      c->proc = 0;
      intena = c->intena;
      swtch(oldcontext, c->scheduler);
      mycpu()->intena = intena;
    }
  }
}

// Round-robin scheduler.
// The same variable is used by all CPUs to determine the starting index.
// It is protected by the process table lock, so no additional lock is
// required.
static int rrindex;

static struct proc *
roundrobin()
{
  // cprintf("applying RR\n");
  // Loop over process table looking for process to run.
  for(int i = 0; i < NPROC; i++) {
    struct proc *p = &ptable.proc[(i + rrindex + 1) % NPROC];
    if(p->state != RUNNABLE)
      continue;
    rrindex = p - ptable.proc;
    adjustallpticks(rrindex); // hw 3 step 2|6 : adjust ticks
    return p;
  }
  adjustallpticks(rrindex); // hw 3 step 2|6 : adjust ticks
  return 0;
}

static struct proc *shortestprocessnext(){
  // cprintf("applying SPN\n"); works more/less, some ticks missed
  static struct proc *running[NCPU]={0};
  static int openind=-1;
  // pick a previous processes already ran
  for(int i=0; i<NCPU; i++){
    if(running[i]!=0 && running[i]->state==RUNNABLE){
      adjustallpticks(running[i]-ptable.proc);
      return running[i];
    }
    openind=i; // state of the process isn't an active one, then it's free
  }
  int num=-1; // num now used to keep track of index for lowest proc
  for(int i=0; i<NPROC; i++){
    if((ptable.proc[i].state != RUNNABLE) || (ptable.proc[i].kernelmode==1)){
      continue;
    }
    if(ptable.proc[i].eticks<0){ // give first priority to neg predict_cpu()
      num=i;
      break;
    }
    if(num<0){ // first proc availible edge case
      num=i;
      continue;
    }else if(ptable.proc[i].eticks<=ptable.proc[num].eticks){
      num=i;
    }
  }
  if(num>-1){
    // save chosen proc into open spot
    running[openind]=&ptable.proc[num];
    adjustallpticks(num); // hw 3 step 2|6 : adjust ticks
    return &ptable.proc[num];
  }
  adjustallpticks(num); // hw 3 step 2|6 : adjust ticks
  return 0;
}

static struct proc *shortestremainingtime(){
  // cprintf("applying SRT\n");
  int num=-1; // num now used to keep track of index
  // Loop over process table looking for shortest process to run.
  for(int i = 0; i < NPROC; i++){
    if((ptable.proc[i].state != RUNNABLE) || (ptable.proc[i].kernelmode==1)){
      continue;
    }
    if(ptable.proc[i].eticks<0){ // give first priority to neg predict_cpu()
      num=i;
      break;
    }
    if(num<0){ // first proc availible edge case
      num=i;
      continue;
    }
    int comp=ptable.proc[i].eticks-ptable.proc[i].tick.pt_cpu;
    int org=ptable.proc[num].eticks-ptable.proc[num].tick.pt_cpu;
    if(comp<org){
      num=i;
    }
  }
  if(num>-1){
    adjustallpticks(num); // hw 3 step 2|6 : adjust ticks
    return &ptable.proc[num];
  }
  adjustallpticks(num); // hw 3 step 2|6 : adjust ticks
  return 0;
}

static struct proc *highestresponseratio(){
  // cprintf("applying HRRN\n");
  static struct proc *running[NCPU]={0};
  static int openind=-1;
  // pick a processes that had already ran
  for(int i=0; i<NCPU; i++){
    if(running[i]!=0 && running[i]->state==RUNNABLE){
      adjustallpticks(running[i]-ptable.proc);
      return running[i];
    }
    openind=i; // state of the process isn't an active one, then it's free
  }
  int num=-1; // num now used to keep track of index
  for(int i = 0; i < NPROC; i++){
    if((ptable.proc[i].state != RUNNABLE) || (ptable.proc[i].kernelmode==1)){
      continue;
    }
    if(ptable.proc[i].eticks<0){ // give first priority to neg predict_cpu()
      num=i;
      break;
    }else if(num<0){ // first proc availible edge case
      num=i;
    }else{
      float org=(ptable.proc[num].tick.pt_wait+(ptable.proc[num].eticks-ptable.proc[num].tick.pt_cpu)) /
                (ptable.proc[num].eticks-ptable.proc[num].tick.pt_cpu);
      float comp=(ptable.proc[i].tick.pt_wait+(ptable.proc[i].eticks-ptable.proc[i].tick.pt_cpu)) /
                (ptable.proc[i].eticks-ptable.proc[i].tick.pt_cpu);
      if(comp>org){
        num=i;
      }
    }
  }
  if(num>-1){
    // save chosen proc into open spot
    running[openind]=&ptable.proc[num];
    adjustallpticks(num); // hw 3 step 2|6 : adjust ticks
    return &ptable.proc[num];
  }
  adjustallpticks(num); // hw 3 step 2|6 : adjust ticks
  return 0;
}

// Called from timer interrupt to reschedule the CPU.
void
reschedule(void)
{
  struct cpu *c = mycpu();

  acquire(&ptable.lock); // nlci+1
  if(c->proc) {
    if(c->proc->state != RUNNING)
      panic("current process not in running state");
    c->proc->state = RUNNABLE;
  }
  sched();
  // NOTE: there is a race here.  We need to release the process
  // table lock before idling the CPU, but as soon as we do, it
  // is possible that an an event on another CPU could cause a process
  // to become ready to run.  The undesirable (but non-catastrophic)
  // consequence of such an occurrence is that this CPU will idle until
  // the next timer interrupt, when in fact it could have been doing
  // useful work.  To do better than this, we would need to arrange
  // for a CPU releasing the process table lock to interrupt all other
  // CPUs if there could be any runnable processes.
  release(&ptable.lock);
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);

    unix_init(2);
    // no need for log cuz not writing, at least no need to implement crash recovery
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";

    // For CPU tick info & call-stack printing
    cprintf("%d %s %s; real:%d cpu:%d wait:%d sleep:%d", 
        p->pid, state, p->name, p->tick.pt_real, 
        p->tick.pt_cpu, p->tick.pt_wait, p->tick.pt_sleep);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }

    // For CPU mem page info and kernel stack
    // cprintf("%d %s %s");
    cprintf("\n");
  }
  // step 2 hw3 done (only need a way to calculate loadavg, for another part)
  // floatloadavg=0.0;
  // cprintf("uptime:%d runnables:%d ", ticks, runnables); // create on modifier to print load avg
  // cprintf("loadavg:%d", (loadavg/100)%100); // before dec
  // cprintf(".%d%d%\n", loadavg/10%10, loadavg%10); // after dec
}

void kforkret(void (*func)(void)){
  static int first=1;
  release(&ptable.lock);
  if(first){
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }
  func();
}

void kfork(void (*func)(void)){
  // allocate process table entry and kernel stack
  struct proc *p=allocproc();
  if(p==0){
    panic("no free processes found for kfork");
  }
  
  // copy process' parent's (init's) page table
  // if((p->pgdir=copyuvm(ptable.proc[0].pgdir, ptable.proc[0].sz))==0){
  //   kfree(p->kstack);
  //   p->kstack=0;
  //   p->state=UNUSED;
  //   panic("failure to copy pagetable to kfork");
  // }

  // setup new process stack
  p->context->eip=(uint)kforkret; // change the instr ptr to *func
  // char *ptf=(char *) p->tf - 4; // overwrite trapret to func
  // *(uint*)ptf=(uint)func; // overwrite trapret to func
  p->parent=&(ptable.proc[0]); // pid 1 is 0th index, first process = init
  p->sz=ptable.proc[0].sz; // copy process memory (bytes) size from init process
  *(p->tf)=*(ptable.proc[0].tf); // copy trapframe from init process
  safestrcpy(p->name, "kfork", 6);
  for(int i = 0; i < NOFILE; i++) // copy file ref count
    if(ptable.proc[0].ofile[i])
      p->ofile[i] = filedup(ptable.proc[0].ofile[i]);
  p->cwd=idup(ptable.proc[0].cwd); // set it to root inode
  p->tf->eax=0; // give trapframe a return of 0

  // set process to runnable
  acquire(&(ptable.lock));
  p->state=RUNNABLE;
  release(&(ptable.lock));
  p->kernelmode=1; // hw 3 step 6 ; is a kernel thread
}

int sigsend(int pid, int sig){
  struct proc *p;
  acquire(&(ptable.lock));
  for(int i=0; i<64; i++){
    if(ptable.proc[i].state>1 || 4<ptable.proc[i].state){
      continue;
    }else if(ptable.proc[i].pid==pid){
      p=&(ptable.proc[i]);
      goto found;
    }
  }
  release(&(ptable.lock));
  return -1;
  found:
    release(&(ptable.lock));
    acquire(&(p->pendwrite));
    p->pendingsig|=sig;
    release(&(p->pendwrite));
    if(holdingsleep(&(p->pendsleep))){
      releasesleep(&(p->pendsleep));
    }
    return 0;
}

int sigsethandler(int sig, void (*hand)(int sig)){
  struct proc *p=myproc();
  if(myproc()->killed){
    return -1;
  }
  if((int)hand<=0){
    p->func[sig]=(void (*) (int))-1;
  }else if((int)hand==-2){
    p->blockedsig|=1<<sig;
    p->func[sig]=(void (*) (int))-2;
  }else{
    p->func[sig]=hand;
  }
  return 0;
}

void sigreturn(void){
  // clear signal-related stack frame
  // 2) get signalnum from user stack and remove from mask
  myproc()->tf->esp+=4; // +4 to skip trampoline address
  int shift=*(int*)(myproc()->tf->esp);
  myproc()->blockedsig&=~(1<<shift);
  // 1) restore trapframe
  myproc()->tf->esp+=4; // +4 to skip signum
  struct trapframe prevtf=*(struct trapframe*)(myproc()->tf->esp);
  *(myproc()->tf)=prevtf; // also moved trapframe esp to where it was before signal stackframe
}

int siggetmask(void){
  struct proc *p=myproc();
  if(p->killed){
    return -1;
  }
  return p->blockedsig;
}

int sigsetmask(int *maskp){
  struct proc *p=myproc();
  if(p->killed){
    return -1;
  }
  int oldmaskp=p->blockedsig;
  p->blockedsig=*maskp;
  *maskp=oldmaskp;
  return 0;
}

int sigpause(int mask){
  struct proc *p=myproc();
  if(p->killed){
    return -1;
  }
  int oldmask=p->blockedsig;
  p->blockedsig=mask;
  while((p->pendingsig & ~p->blockedsig)==0)
    acquiresleep(&(p->pendsleep));
  p->blockedsig=oldmask;
  return 0;
}

void getPtable(struct proc *p){
  acquire(&(ptable.lock));
  for(int i=0; i<NPROC; i++){
    p[i]=ptable.proc[i];
  }
  release(&(ptable.lock));
}

int getpt_real(struct proc *p){
  return p->tick.pt_real;
}

int getpt_cpu(struct proc *p){
  return p->tick.pt_cpu;
}

int getpt_wait(struct proc *p){
  return p->tick.pt_wait;
}

int getpt_sleep(struct proc *p){
  return p->tick.pt_sleep;
}

int getloadavg(){
  return loadavg;
}

void predict_cpu(int ticks){
  myproc()->eticks=ticks;
}

// Adjust all valid processes' ticks accordingly
// Given argument is the index of chosen process in ptable
static void adjustallpticks(int ind){
  for(int i=0; i<NPROC; i++){
    struct proc *p=&ptable.proc[i];
    enum procstate ps=p->state;
    if(ps==SLEEPING){
      p->tick.pt_sleep++;
    }else if(i==ind){
      // must be compared before ps==RUNNABLE
      // b/c chosen proc is still RUNNABLE state
      // processes in RUNNING state will be 
      // adjusted by the respective CPU
      p->tick.pt_cpu++;
    }else if(ps==RUNNABLE){
      p->tick.pt_wait++;
    }
    if(ps!=UNUSED){ // increment all valid processes
      p->tick.pt_real++;
    }
  }
}

int getppid(struct proc *p){
  if(p){
    return p->pid;
  }
  return myproc()->pid;
}