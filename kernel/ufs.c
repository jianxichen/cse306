// Used only to read Uv5 ondisk inodes 

#include "ufs.h"
#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "semaphore.h"
#include "fs.h"
#include "file.h"
#include "buf.h"

#define min(a, b) ((a) < (b) ? (a) : (b))
struct unix_superb u_sb;
struct unix_dinode u_inode; // Not used

static void read_unixsb(int dev, struct unix_superb* sb){
    struct buf *bp;
    bp=bread(dev, 1); // Sector = Blocks
    memmove(sb, bp->data, sizeof(*sb));
    brelse(bp);
}

void unix_init(int dev){
    read_unixsb(dev, &u_sb);
    int time=u_sb.s_time[0]<<16 | u_sb.s_time[1];
    cprintf("unix: isize %d \n\
            fsize %d \n\
            nfree %d \n\
            s_ninode %d \n\
            s_time{0,1} %d\n",
            u_sb.s_isize, u_sb.s_fsize, u_sb.s_nfree, \
            u_sb.s_ninode, time);
}