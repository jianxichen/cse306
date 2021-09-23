For this homework, we essentially added our own additional systemcall to the xv6 operating system. 
Specifically, we were able to use the PS/2 controller to allow and detect for mouse inputs.

In order to make this happen, first had to decouple the potential mouse and keyboards data given by
the PS/2 Controller. We achieved this by, first, within the kbdintr() func in the kbdkernel/kbd.c, we added a Bit-0
checking system to make sure that the data that will be read in port 0x60 of PS/2 is valid data by AND the received
status byte to 0x1. Then we checked whether if the Bit-5 checking system by ANDing to 0x20. If Bit-5 is 0, then that
means it's a keyboard data. Then it will exit out of the while and escalate to consoleintr(kbdgetc) because mouse data
preceeds the keyboard data. If Bit-5 is 1, then we will ignore that packet and proceed to the next one if there is (which
is determined by the Bit-0 on the next status byte). Hence, the mouse data and keyboard data are now decoupled.

Next, to allow for PS/2 to detect mouse events, we have sent the COMPAQ bytes (and etc...). However, we also decided
to allow for timing by implementing a wait_read() and wait_write() which puts the CPU on a while-loop until the status
byte returns the allowable Bit-#. Doing this wait timing and polling eliminates the nuance of having to implement a wait 
to read/write for every action to the ports. All of this "COMPAQ setup" is done in the mouseinit() and placed before the 
uartinit() function because that deals serial ports and we need to initialize serial ports first.

For our mouseintr(), we decided to create a mouse packet structure dedicated to the mouse packet for easier data extract 
simplicity for Exercise 3, where we will need to spit out data about changes and events happening in the mouse via a user
program. Within the mouseintr(), all we do is collect data from the ports (with inclusion of the waits), create a mouse_packet
instance, and put the data from the port into that structure instance, then add it into our circular buffer. This mouse interrupt
will also call a releasesleep(sleeplock*) to wake any processes that are slept on that sleeplock.

To implement the system call of readmouse(char*), we accessed the files of kernel/syscall.c, kernel/syscall.h, user/usys.S,
kernel/defs.h, and user/user.h. Following suit with existing systemcalls, we will put the readmouse(char*) into our kernel/mouse.c
file because it pertains to the mouse. From this system call, we see that when a user program will know of readmouse(char*) from
the prototype declaration user/user.h, then because of usys.S, that function will get mapped to a systemcall number, which we had
defined in kernel/syscall.h. There, it will proceed to go to the syscall() located kernel/syscall.c to actually run the function as
there is a function pointer array located in syscall.c. All in all, the function that will call to actually use readmouse(char*) is 
the sys_readmouse() function, in which we decided will be kept in kernel/mouse.c. There, the function returns a readmouse(char*) that's
implemented inside kernel/mouse.c as well. All it's doing is have sys_readmouse() pass in a char array, in which, will filled in by 
the kernel's readmouse(), which reads from the buffer with sleeplock if there is not enough data in buffer, and then readmouse() returns
its success with 0 and failure with -1. Finally, in the user program, we pass into the readmouse() as a structure because memory-wise, it is
the same data structure and both are 3 bytes long, therefore they are complimentary and will not produce negative sideeffects.

Our test user program that we created for Exercise 3 is "ml". When launched via terminal, if user has previously moved their mouse prior to
running the program, you will receive a sudden amount of outputs because everytime there is a mouse interrupt, it adds into the buffer.
Any overflow of mouse events mouse (only 129 packets are kept, we made our buffer size small) will be deleted/ignored as stated by
the homework. By launching user program "ml", it will finally read and remove the mouse packet entries from the buffer. Then it will be 
kept in a sleep state by the sleeplock until there is a mouse interrupt that wakes it up.

Assumptions:

1. We assumed that the 3-byte packet is continiously sent by PS/2 controller.

How to build and run:

```sh
make CPUS=1 qemu
```

After xv6 booting up on qemu, type "ls" command and will see a "ml" user program shown on the list.
Type "ml" to run the program.

Printout Example of demo program "ml":

 |----------------------------------------------- 1st Byte (Hex)
 |  |-------------------------------------------- 2nd Byte (Hex)
 |  | |------------------------------------------ 3rd Byte (Hex)
 |  | |   |-------------------------------------- Y overflow
 |  | |   ||------------------------------------- X overflow
 |  | |   || |----------------------------------- Y sign Bit
 |  | |   || ||---------------------------------- X sign Bit
 |  | |   || || |-------------------------------- Always 1 Bit
 |  | |   || || | |------------------------------ Middle Button
 |  | |   || || | ||----------------------------- Right Button
 |  | |   || || | |||---------------------------- Left Button
 |  | |   || || | |||     |---------------------- Delta X (2's)
 |  | |   || || | |||     |  |------------------- Delta Y (2's)
 |  | |   || || | |||     |  |     |------------- Mouse Action
 9  0 0 = 00 00 1 001 --  0  0 ==> Left clicked
18 F0 9 = 00 01 1 000 -- 240 9 ==> Move (-6, 9)
