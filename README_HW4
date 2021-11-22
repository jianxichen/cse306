Exercise 1:
For this exercise, in order to have QEMU boot xv6 with 2 IDE controllers, I changed the Makefile target in each "make" command to include 
disknames of "disk2.img" and "disk3.img". Within each disk target (i.e. "disk2.img:"), I used diskdump command to simply copy-paste over the 
backup file of "unix-v5-boot.img", of which is located in the root folder. With this addition, I can ensure that every "make clean; make qemu" 
will give me a new, fresh unedited UnixV5 disk image to work with. I applied the diskdump command "dd if=unix-v5-boot.img of=disk3.img" to 
both "disk2.img" and "disk3.img" just to ensure that (1) both drives for IDE2 controller are initialized and (2) I can always read from one or 
the other disk if need be –– although that was never the case.

Exercise 2:
In order to complete this part, I refactored a files which were primarily "fs.c", "file.h", "file.c", "bio.c". I also added a new source file 
called "ide2.c" in order to separate the 2 different IDE controller for both purpose of isolation (so I know for sure which IDE I'm trying 
to use) and simplicity as I don't need to account for both IDE ports in a single source file (which makes debugging easier). The main 
difference between ide2.c and ide.c was just the port reference since IDE2 controller has its own set of ports in order to access it. Another 
file that was added was "ufs.c" and "ufs.h". This allowed for easier conversion and readibility when attempting to read the unix boot image 
and then trying to convert the on-disk unix inodes into xv6's in-memory inodes. I also found that giving a separate source and header file for 
the Unix data structures greatly simplified the disk-reading and disk-writing in the long run. However, in the actual procedure of reading and 
traversing through the inodes, I did not give "ufs" its own separate method like in ide2.c. Instead, I modified it within "fs.c" because I 
found that since I was able to convert a unix disk inode into a xv6 memory inode meanwhile able to maintain the same information (w/o losing 
information during conversion), I could use the same methods and techniques as if they were the xv6 memory inodes (after the conversion). 
The last major change was that within namex (and all methods that namex calls and calls namex), I had to include a way to incorporate the 
absolute pathname of starting with "%" in order access the unix file system instead of xv6's. I had to modify existing systemcalls such 
as "ls" in order to incorporate the changes such as device# origin when traversing and iterating through lists. To account for large files, 
I modified the method readi() so then it will check if the file is from the unix file system and see if its a large file, and if those 2 are 
true, then it will accordingly access each INDIRECT block with consideration of file* offset.

Exercise 3:
In order to complete this exercise, I modified syscalls like mkdir and rm. In addition, I modified buffer write functions that would've first 
written a log into log.c to do not write in log.c if the buffer being written is for device 2. Other example functions include dirlink, dirunlink
I decided on this change because providing a crash recovery would simply be a little too much work when all we're doing is just adding inodes and 
writing data. I also used the freelists to find open datablocks and inodes. All in all, this was similar to Exercise 2, but I made a design 
choice to immediately write straight into the disk rather than log it due to the fact that refactoring log.c to have a crash recovery for the 
unixv5 file system wasn't signficant.