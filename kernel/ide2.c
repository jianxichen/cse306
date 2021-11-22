// Simple PIO-based (non-DMA) IDE driver code.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "semaphore.h"
#include "buf.h"
#include "proc.h"

#define SECTOR_SIZE   512
#define IDE_BSY       0x80
#define IDE_DRDY      0x40
#define IDE_DF        0x20
#define IDE_ERR       0x01

#define IDE_CMD_READ  0x20
#define IDE_CMD_WRITE 0x30
#define IDE_CMD_RDMUL 0xc4
#define IDE_CMD_WRMUL 0xc5

// idequeue points to the buf now being read/written to the disk.
// idequeue->qnext points to the next buf to be processed.
// You must hold idelock while manipulating queue.

static struct spinlock idelock;
static struct buf *idequeue;

#define IDE2_BASE1 0x170
#define IDE2_BASE2 0x376

static int havedisk2;
static int havedisk3;
static void ide2start(struct buf*);

// Wait for IDE disk to become ready.
static int
ide2wait(int checkerr)
{
  int r;

  while(((r = inb(IDE2_BASE1+7)) & (IDE_BSY|IDE_DRDY)) != IDE_DRDY)
    ;
  if(checkerr && (r & (IDE_DF|IDE_ERR)) != 0)
    return -1;
  return 0;
}

void
ide2init(void)
{
  int i;

  initlock(&idelock, "ide");
  ioapicenable(IRQ_IDE2, ncpu - 1);
  ide2wait(0);

  // Check if disk 2 is present
  outb(IDE2_BASE1+6, 0xe0 | (0<<4));
  for(i=0; i<1000; i++){
    if(inb(IDE2_BASE1+7) != 0){
      havedisk2 = 1;
      cprintf("found disk 2\n");
      break;
    }
  }

  // Check if disk 3 is present
  outb(IDE2_BASE1+6, 0xe0 | (1<<4));
  for(i=0; i<1000; i++){
    if(inb(IDE2_BASE1+7) != 0){
      havedisk3 = 1;
      cprintf("found disk 3\n");
      break;
    }
  }

  // Switch back to disk 2.
  outb(IDE2_BASE1+6, 0xe0 | (0<<4));
}

// Start the request for b.  Caller must hold idelock.
static void
ide2start(struct buf *b)
{
  if(b == 0)
    panic("idestart");
  if(b->blockno >= 4000)
    panic("incorrect blockno");
  int sector_per_block =  BSIZE/SECTOR_SIZE;
  int sector = b->blockno * sector_per_block;
  int read_cmd = (sector_per_block == 1) ? IDE_CMD_READ :  IDE_CMD_RDMUL;
  int write_cmd = (sector_per_block == 1) ? IDE_CMD_WRITE : IDE_CMD_WRMUL;

  if (sector_per_block > 7) panic("idestart");

  ide2wait(0);
  outb(IDE2_BASE2, 0);  // generate interrupt
  outb(IDE2_BASE1+2, sector_per_block);  // number of sectors
  outb(IDE2_BASE1+3, sector & 0xff);
  outb(IDE2_BASE1+4, (sector >> 8) & 0xff);
  outb(IDE2_BASE1+5, (sector >> 16) & 0xff);
  outb(IDE2_BASE1+6, 0xe0 | ((b->dev&1)<<4) | ((sector>>24)&0x0f));
  if(b->flags & B_DIRTY){
    outb(IDE2_BASE1+7, write_cmd);
    outsl(IDE2_BASE1, b->data, BSIZE/4);
  } else {
    outb(IDE2_BASE1+7, read_cmd);
  }
}

// Interrupt handler.
void
ide2intr(void)
{
  struct buf *b;

  // First queued buffer is the active request.
  acquire(&idelock);

  if((b = idequeue) == 0){
    release(&idelock);
    return;
  }
  idequeue = b->qnext;

  // Read data if needed.
  if(!(b->flags & B_DIRTY) && ide2wait(1) >= 0)
    insl(IDE2_BASE1, b->data, BSIZE/4);

  // Wake process waiting for this buf.
  b->flags |= B_VALID;
  b->flags &= ~B_DIRTY;
  // wakeup(b); CHANGES HERE
  sem_V(&b->sem);

  // Start disk on next buf in queue.
  if(idequeue != 0)
    ide2start(idequeue);

  release(&idelock);
}

//PAGEBREAK!
// Sync buf with disk.
// If B_DIRTY is set, write buf to disk, clear B_DIRTY, set B_VALID.
// Else if B_VALID is not set, read buf from disk, set B_VALID.
void
ide2rw(struct buf *b)
{
  struct buf **pp;

  if(!holdingsleep(&b->lock))
    panic("ide2rw: buf not locked");
  if((b->flags & (B_VALID|B_DIRTY)) == B_VALID)
    panic("ide2rw: nothing to do");
  if(b->dev == 2 && !havedisk2)
    panic("ide2rw: ide disk 2 not present");

  sem_init(&b->sem, 0);
  
  acquire(&idelock);  //DOC:acquire-lock

  // Append b to idequeue.
  b->qnext = 0;
  for(pp=&idequeue; *pp; pp=&(*pp)->qnext)  //DOC:insert-queue
    ;
  *pp = b;

  // Start disk if necessary.
  if(idequeue == b)
    ide2start(b);

  // Wait for request to finish.
  // while((b->flags & (B_VALID|B_DIRTY)) != B_VALID){ CHANGES HERE
  //   sleep(b, &idelock); CHANGES HERE
  // }


  // release(&idelock); CHANGES HERE
  release(&idelock);
  sem_P(&b->sem);
}
