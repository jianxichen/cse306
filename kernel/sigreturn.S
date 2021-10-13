#include "syscall.h"
#include "traps.h"

.globl sigreturn_bounce

sigreturn_bounce:
    movl $SYS_sigreturn, %eax   # put syscall #
    int $T_SYSCALL              # generate interrupt w/ syscall trapno
