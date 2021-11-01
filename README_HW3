To call scheduler when about to idle but exists a runnable process:
For this to happen, I simply created a public "flag" variable in proc.c (where it's also declared in defs.h), so when a newly created process 
is created via allocproc(), it will raise that flag, and within an idle(), if an CPU process sees that flag, it will immediately call the 
scheduler instead of waitnig for a timer interrupt for a reschedule.

Instrumentation code to track processes' ticks, uptime, load average, and runnable/running processes:
In order track and update these data, I put them all in the scheduler. Since a timer interrupt will cause a reschedule and an idle as well, 
this means that putting it in a scheduler will be the best way to update/maintain these data. Hence, the ticks and counting of the 
processes are updated in the scheduler.

Predict_CPU(int):
This was a simply syscall. All it does is change the eticks variable which an int attribute in a process structure.

Creating each scheduler:
For the non-preemptive scheduler, I had those policies maintain an struct proc * array so it has knowledge of what processes has been ran before 
that can be resumed. If nothing previously ran can be resumed, then it will pick a new runnable process in the ptable and record it onto the array.

Changing scheduling policy:
I have adapted the defs.h to have #define POLICY in the proc.c section. Depending on the value (0 for RR, 1 for SPN, 2 for SRT, 3 for HRRN), when 
compiling the kernel, it was change the scheduling policy. However, it will always take care of the kernel is a roundrobin fashion.

To find load average p constant:
Since every tick in xv6 is ~10ms. That's equivalent to ~100 ticks per seconds. Since it needs to be at 10% of its initial load_average 
within 30 seconds if the current CPU load is 0. This totals to ~3000 timer interrupt ticks for those 30 seconds. Doing the math, 
V_t = (p) * 0 + (1-p) * V_[t-1], where V is the load_average, leads to a linear recursive relation of 0.1V = (1-p)^(3000). This results 
in p = ~0.0007672. We can verify this by (1-0.0007672)^(3000) = (0.9992328)^(3000) =  ~0.1. Hence after 3000 ticks, we will get 10% of the initial 
load_average when the current CPU load is 0 throughout all 30 seconds or 3000 ticks.

Imeplementation to print load_average:
In order to print the load_average, I used an integer imeplementation. This means that load_average is kept as an integer that represents 
##.## meanwhile actually being an integer value of ####. This also means that when obtaining the new load_average, I have to use integer 
multiplication first before division to prevent as much integer truncation. This means 0.0007672 * p = 7672 * p / 1000000, and to convert to 
####, we would need to add multiply an additional 10,000 to move the digits left. But when doing (1-p) * V_[t-1], we only need to do 
9992328 * load_average / 10000000 because load_average itself is already adjusted to #### therefore we don't need to multiply it by 1000.

Coin Flipping:
In order to coinflip, I applied the same methodology as in the load average. I used an integer multiplication and then modulo it by 100 so I 
can obtain a random number ranging from 0-99. I also did not instantiate an int variable called "rando" in hope that it will help provide 
physical randomness. I also had another int variable called changer so there can be some sort of "ongoingness" to the number generation.

Worker and Driver Process:
To find how many lines of code a process runs per tick, I simply made a loop of printf() and added a cprintf() in the trap timer interrupt. 
For the worker and driver process, I hard-capped the number of worker processes to 6, so if there are currently 6 active/was-active process, 
there will be a less and less chance of creating a new process. In addition, if the total of the number of processes have ticks higher than 10, 
I derived a formula that made it so it will be harder to roll a tail as there exists more processes that have estimated ticks over 10. For the 
sleeping of driver process, the sleep syscall was already given on default xv6.

Results:
As we can expect, depending on the policy, the favorites will change and we will see certain processes with larger or smaller amount of ticks run 
a tad bit more often than the opposite. An example is that SPN will always run the shortest processes with the least number of ticks.