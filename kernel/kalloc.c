// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"

void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file
                   // defined by the kernel linker script in kernel.ld

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
} kmem;

// There exists max (PHYSTOP)>>12 PPNs to use, indexed by PPN
unsigned char pgrefcounter[PHYSTOP>>12]; // Refcnt for Pt entry
struct spinlock pgreflock; // Lock pgrefcounter

// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void
kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem");
  initlock(&pgreflock, "Pgref");
  kmem.use_lock = 0;
  freerange(vstart, vend);
}

void
kinit2(void *vstart, void *vend)
{
  freerange(vstart, vend);
  kmem.use_lock = 1;
}

void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
    kfree(p);
}
//PAGEBREAK: 21
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(char *v)
{
  struct run *r;

  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(v, 1, PGSIZE);

  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = (struct run*)v;
  r->next = kmem.freelist;
  kmem.freelist = r;
  if(kmem.use_lock)
    release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
// Returned address pointer is virtual-adjusted
char*
kalloc(void)
{
  struct run *r;

  if(kmem.use_lock){
    acquire(&kmem.lock);
  }
  r = kmem.freelist;
  if(r){
    // Freelist is from highest virt. addr to lowest virt.addr
    // Within run *r points to next left free addr of r addr
    kmem.freelist = r->next;
  }
  if(kmem.use_lock){
    release(&kmem.lock);
  }
  return (char*)r;
}

void chgpgrefc(void *va, uint dif){
  if(kmem.use_lock){
    // Purpose is for use after kinits
    // indexed PPN for ref counter is (r-KERNBASE)>>12
    unsigned int ppn=((uint)va - KERNBASE)>>12;
    acquire(&pgreflock);
    pgrefcounter[ppn]+=dif;
    // cprintf("changing ppn 0x%x by %d\n", ppn, dif);
    release(&pgreflock);
  }
}

uchar getpgrefc(void *va){
  if(kmem.use_lock){
    // Purpose is for use after kinits
    // indexed PPN for ref counter is (r-KERNBASE)>>12
    unsigned int ppn=((uint)va - KERNBASE)>>12;
    acquire(&pgreflock);
    uchar c=pgrefcounter[ppn];
    release(&pgreflock);
    return c;
  }
  panic("somehow ended up here before kinit2 done?");
  return 0;
}