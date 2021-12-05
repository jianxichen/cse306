#include "kernel/types.h"
#include "kernel/stat.h"
#include "user.h"

#define PROCNUM 50 // Would effectively make 100 children (100 forks)

int main(){
    printf(1, "");
    int something[2000]={1};
    printf(1, "USERPROCsomething%d\n", something[2]);
    for(int i=0; i<PROCNUM; i++){
        if((fork())==0){
            something[2]=3;
            sleep(2000);
            exit();
        }
        sleep(2000);
        if((fork())==0){
            sleep(2000);
            exit();
        }
        sleep(2000);
    }
    fork();
    exit();
}