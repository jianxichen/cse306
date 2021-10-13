For exercise 1, the goal was to create a semaphore. The data structure for this consisted of a spinlock and an initial 
value for each semaphore instance. For the two functions associated with the semaphore, sem_P() and sem_V(), when there exists 
no more "balls" to take from, the process that's attempting to take will be kept on sleep() on the spinlock's address as channel. 

For exercise 2, the goal was to create a kernel thread that acted like process but on the kernel side. The design approach I took 
for this was to reference pre-existing functions that relate to process-creation like userinit() or fork(). Within kfork(), I created
another function called kforkret() within the CPU jump straight into kforkret() after kfork() in order to release the ptable lock, that 
it would have because it's the first time that process is running. I also had kforkret() take in the function pointer as a parameter so 
that the process will be able to run the dedicated function. Then it is called inside kforkret(). After it is done, it should get reaped 
by the init process like normal.

For exercise 3, I modified the proc structure in proc.h to have a sleeplock and a spinlock. The sleeplock is for simplicity sake to allow 
the process to go to sleep and the spinlock is to allow the mutual exlucion during any attemps to into buffer. The the handling of the signals, 
I decided to have the signal serving within the TF->TRAPNO=SYSCALL condition of the trap(). This is because the only time when we can guarantee 
that the current trap is due to a process is if it is a syscall, otherwise putting at the end of switch-case statement allows arbitrary traps 
like hardware traps to traverse down and call upon the myproc() method which is not available when the trap vector is due to a hardware. I also 
provided a signal.h for future proof in cases where we will need to define each signal number. Because I included sleeplock and spinlock in the 
proc structure, I had to modify other source files so the proc.h comes after spinlock.h and sleeplock.h. In order to allow for user address 
space for the signal trampoline, I created a sigreturn.S assembly which moves the sigreturn() sycall number into %eax and then call for an 
interrupt. In regards to the sending of signals from one process to annother, sigsend() will acquire lock on proc's pendingsig variable 
in order to give the pending signals and wake the recipient process if that process is put on sleep with the signal function sigpause(). In 
regards how each process structure will have their own signal handler for all 32 different signals, I have applied an void(*func[32])(int) 
to the process structure. This way, the process structure will have like a book-keeping for each individual function handler dedicated to each 
unique signal.