#include "kernel/types.h"
#include "kernel/stat.h"
#include "user.h"

static int seed1=56631;
static int rando; // This will be a random piece of memory for the RNG
static int seed2=5483363;
static int changer=142;

struct ptimes {
  int pt_real;   // Ticks since process creation
  int pt_cpu;    // Ticks of CPU time consumed
  int pt_wait;   // Ticks spent waiting for CPU
  int pt_sleep;  // Ticks spent sleeping
};

// it takes ~10 printf() for 1 tick

int flipcoin(){
    changer=(seed1*100/((int)&changer)*rando*100*seed2/changer);
    return changer;
}

int main(){
    predict_cpu(-1);
    // flip coin
    // coin says yes then fork
    // else wait
    // sleep
    // Will keep track of ended processes so we can actively maintain a 90% under 10 ticks

    int n=0; // number of total workers
    int r=0; // number of running workers
    int t10=0; // number of reaped workers over 10 ticks
    int stop=0;
    struct ptimes times={-1,-1,-1,-1};
    while(stop==0){
        int coin=flipcoin()%100;
        if(r<6 && (coin<(((6-r)*100)/6)%100)){
            n++;
            r++;
            if(fork()==0){
                // Child
                int ticks=1;
                while(1){
                    int coin2=flipcoin()%100;
                    if(coin2<((n-t10+1)*100/(t10+1+ticks))%100){
                        // this formula creates biased coin
                        // if mostly t10, then n-(n-1)+1 / (n-1)+2 = 2/(n+1), 
                        // thus more biased to get a tick as there exists more heads with ticks
                        // if less t10, then n-(1)+1 / (1)+2 = n/3
                        // in initial phase where t10 and n is 0, it will be unbiased
                        ticks++;
                        continue;
                    }
                    break;
                }
                predict_cpu(ticks);
                // execute time
                for(int i=0; i<ticks; i++){
                    for(int x=0; x<10; x++){
                        printf(1, ""); // print nothing to console
                    }
                }
                exit();
            }
        }else{
            // dont create a process
            // see if it has a process and take 1 out
            if(r>3){
                wait(&times);
                printf(1, "real:%d  cpu:%d  wait:%d  sleep:%d", times.pt_real, times.pt_cpu, times.pt_wait, times.pt_sleep);
                r--;
            }
        }
        if(n>1 && (t10*100/n)<=10){
            stop=1;
            break;
        }
        sleep(1);
    }

    // finalize leftover worker process
    while(r){
        wait(&times);
        printf(1, "real:%d  cpu:%d  wait:%d  sleep:%d", times.pt_real, times.pt_cpu, times.pt_wait, times.pt_sleep);
        r--;
    }
    exit();
}

/*
previous worker code
while(1){
        int coin=flipcoin() % 100;
        printf(1, "after coin flip\n");
        struct ptimes time={-1};
        printf(1, "is it this struct\n");
        if(r<6 && coin<(((6-r)*100)/6)%100){
            printf(1, "after r\n");
            printf(1, "after coin flip\n");
            // will create a new process of there is enough space for a process creation
            n++;
            r++;
            printf(1, "attempt to fork %p",&time);
            if(fork()==0){
                // Child
                // roll for ticks
                int ticks=1;
                while(1){
                    int coin2=flipcoin() % 100;
                    if(coin2<((n-t10+1)*100/(t10+1+ticks))%100){
                        // this formula creates biased coin
                        // if mostly t10, then n-(n-1)+1 / (n-1)+2 = 2/(n+1), 
                        // thus more biased to get a tick as there exists more heads with ticks
                        // if less t10, then n-(1)+1 / (1)+2 = n/3
                        // in initial phase where t10 and n is 0, it will be unbiased
                        break;
                    }
                    ticks++;
                }
                predict_cpu(ticks);
                // execute time of instr per tick
                for(int i=0; i<ticks; i++){
                    for(int x=0; x<10; x++){
                        printf(1, ""); // execut arbitrary print()
                    }
                }
                exit();
            }
        }else{
            if(r){
                // printf(1, "1address of struct %p",&time);
                int chldpid=wait(&time);
                printf(1, "worker %d, age %d ticks, consumed %d CPU ticks, waited %d ticks, slept %d ticks", chldpid, time.pt_real, time.pt_cpu, time.pt_wait, time.pt_sleep);
                r--;
            }
        }
        if((t10*100/n)<=10){
            // stop making workers if we find have almost achieved 90% less than 10 tick
            while(r){
                // printf(1, "2address of struct %p",&time);
                int chldpid=wait(&time);
                printf(1, "worker %d, age %d ticks, consumed %d CPU ticks, waited %d ticks, slept %d ticks", chldpid, time.pt_real, time.pt_cpu, time.pt_wait, time.pt_sleep);
                r--;
            }
            break;
        }
        sleep(1);
    }
*/