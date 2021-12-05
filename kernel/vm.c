#include "param.h"
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "elf.h"

extern char data[];  // defined by kernel.ld
pde_t *kpgdir;  // for use in scheduler()

extern unsigned int pgrefcounter[];
extern struct spinlock pgreflock;
extern uint allocpages;

// Set up CPU's kernel segment descriptors.
// Run once on entry on each CPU.
void
seginit(void)
{
  struct cpu *c;

  // Map "logical" addresses to virtual addresses using identity map.
  // Cannot share a CODE descriptor for both kernel and user
  // because it would have to have DPL_USR, but the CPU forbids
  // an interrupt from CPL=0 to DPL=3.
  c = &cpus[cpuid()];
  c->gdt[SEG_KCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, 0);
  c->gdt[SEG_KDATA] = SEG(STA_W, 0, 0xffffffff, 0);
  c->gdt[SEG_UCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, DPL_USER);
  c->gdt[SEG_UDATA] = SEG(STA_W, 0, 0xffffffff, DPL_USER);
  lgdt(c->gdt, sizeof(c->gdt));
}

// Return the address of the PTE in page table pgdir
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages. TLDR: Find the pte_t entry in pgdir via va.
pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde;
  pte_t *pgtab;

  // Convert/Assume pgdir as array of pde_t at ind=0
  // From VA, get Pdirect index offset from pgdir
  // Use index offset to get pde_t from pgdir
  pde = &pgdir[PDX(va)];
  if(*pde & PTE_P){ // Check if Pdirectory entry is Present
    // Access Pdirectory entry address 
    // Extract only the PPN from entry
    // Then adjust PPN to virtual ==> pgtab is virtual-adjusted PPN
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde)); // pgtab points to base/start of fresh Ptable
  } else { // Pdirectory entry is not present
    // Allocates a Ptable
    if(!alloc || (pgtab = (pte_t*)kalloc()) == 0){
      // !alloc is just to set mode where if we should be allocating or not
      // alloc=0 means do not allocate
      return 0;
    }
    // Make sure all those PTE_P bits are zero.
    // pgtab is pointer to the new Ptable
    memset(pgtab, 0, PGSIZE); // Set all values of fresh Ptable to 0
    // The permissions here are overly generous, but they can
    // be further restricted by the permissions in the page table
    // entries, if necessary.
    // Set 20bit (PPN) and 12bit (perms) put into the corresponding Pdirectory entry
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U; // Must save pgtab (virtual-adjusted PPN) as Physical adjusted ==> Save PPN
  }
  return &pgtab[PTX(va)]; // Returns (virtual adjusted PPN) address of PTable entry
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned.
static int
mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm)
{
  char *a, *last;
  pte_t *pte;

  a = (char*)PGROUNDDOWN((uint)va); // round down va to 4096 bdy
  last = (char*)PGROUNDDOWN(((uint)va) + size - 1); // round down to last bdy to be written into 
  for(;;){
    if((pte = walkpgdir(pgdir, a, 1)) == 0){
      // Check if 
      return -1;
    }
    if(*pte & PTE_P){
      // Check if va already has Present entry
      panic("remap");
    }
    *pte = pa | perm | PTE_P; // Ptable entry = PPN | (perms|Present)
    if(a == last) // check if tracker is at last bdy that HAS been written
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// There is one page table per process, plus one that's used when
// a CPU is not running any process (kpgdir). The kernel uses the
// current process's page table during system calls and interrupts;
// page protection bits prevent user code from using the kernel's
// mappings.
//
// setupkvm() and exec() set up every page table like this:
//
//   0..KERNBASE: user memory (text+data+stack+heap), mapped to
//                phys memory allocated by the kernel
//   KERNBASE..KERNBASE+EXTMEM: mapped to 0..EXTMEM (for I/O space)
//   KERNBASE+EXTMEM..data: mapped to EXTMEM..V2P(data)
//                for the kernel's instructions and r/o data
//   data..KERNBASE+PHYSTOP: mapped to V2P(data)..PHYSTOP,
//                                  rw data + free physical memory
//   0xfe000000..0: mapped direct (devices such as ioapic)
//
// The kernel allocates physical memory for its heap and for user memory
// between V2P(end) and the end of physical memory (PHYSTOP)
// (directly addressable from end..P2V(PHYSTOP)).

// This table defines the kernel's mappings, which are present in
// every process's page table.
static struct kmap {
  void *virt;
  uint phys_start;
  uint phys_end;
  int perm;
} kmap[] = {
 { (void*)KERNBASE, 0,             EXTMEM,    PTE_W}, // I/O space
 { (void*)KERNLINK, V2P(KERNLINK), V2P(data), 0},     // kern text+rodata
 { (void*)data,     V2P(data),     PHYSTOP,   PTE_W}, // kern data+memory
 { (void*)DEVSPACE, DEVSPACE,      0,         PTE_W}, // more devices
};
// *virt,           phys_start,    phys_end,  perm

// Set up kernel part of a page table.
pde_t*
setupkvm(void)
{
  pde_t *pgdir;
  struct kmap *k;

  // Attempt to allocate 4KB; meant to be start of Pd
  if((pgdir = (pde_t*)kalloc()) == 0){
    return 0;
  }
  memset(pgdir, 0, PGSIZE); // Set all Pt entries to 0
  if (P2V(PHYSTOP) > (void*)DEVSPACE){
    panic("PHYSTOP too high");
  }
  // For each obj in kmap[], attempt to map in all instance
  for(k = kmap; k < &kmap[NELEM(kmap)]; k++)
    if(mappages(pgdir, k->virt, k->phys_end - k->phys_start,
                (uint)k->phys_start, k->perm) < 0) {
      freevm(pgdir);
      return 0;
    }
  return pgdir;
}

// Allocate one page table for the machine for the kernel address
// space for scheduler processes.
void
kvmalloc(void)
{
  kpgdir = setupkvm();
  switchkvm();
}

// Switch h/w page table register to the kernel-only page table,
// for when no process is running.
void
switchkvm(void)
{
  lcr3(V2P(kpgdir));   // switch to the kernel page table
}

// Switch TSS and h/w page table to correspond to process p.
void
switchuvm(struct proc *p)
{
  if(p == 0)
    panic("switchuvm: no process");
  if(p->kstack == 0)
    panic("switchuvm: no kstack");
  if(p->pgdir == 0)
    panic("switchuvm: no pgdir");

  pushcli();
  mycpu()->gdt[SEG_TSS] = SEG16(STS_T32A, &mycpu()->ts,
                                sizeof(mycpu()->ts)-1, 0);
  mycpu()->gdt[SEG_TSS].s = 0;
  mycpu()->ts.ss0 = SEG_KDATA << 3;
  mycpu()->ts.esp0 = (uint)p->kstack + KSTACKSIZE;
  // setting IOPL=0 in eflags *and* iomb beyond the tss segment limit
  // forbids I/O instructions (e.g., inb and outb) from user space
  mycpu()->ts.iomb = (ushort) 0xFFFF;
  ltr(SEG_TSS << 3);
  lcr3(V2P(p->pgdir));  // switch to process's address space
  popcli();
}

// Load the initcode into address 0 of pgdir.
// sz must be less than a page.
void
inituvm(pde_t *pgdir, char *init, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc(); // Allocate 4KB page, for Pd
  // Mem is virtual addr for kernel to use
  memset(mem, 0, PGSIZE); // Zero out page
  mappages(pgdir, 0, PGSIZE, V2P(mem), PTE_W|PTE_U); // mem VA to PA
  memmove(mem, init, sz);
}

// Load a program segment into pgdir.  addr must be page-aligned
// and the pages from addr to addr+sz must already be mapped.
int
loaduvm(pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz)
{
  uint i, pa, n;
  pte_t *pte;

  if((uint) addr % PGSIZE != 0)
    panic("loaduvm: addr must be page aligned");
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, addr+i, 0)) == 0)
      panic("loaduvm: address should exist");
    pa = PTE_ADDR(*pte);
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if(readi(ip, P2V(pa), offset+i, n) != n)
      return -1;
  }
  return 0;
}

// Allocate page tables and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
int
allocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  char *mem;
  uint a;

  if(newsz >= KERNBASE)
    return 0;
  if(newsz < oldsz)
    return oldsz;

  a = PGROUNDUP(oldsz);
  for(; a < newsz; a += PGSIZE){
    mem = kalloc();
    // cprintf("allocuvm: increment allocp\n");
    allocpages++;
    if(mem == 0){
      cprintf("allocuvm out of memory\n");
      deallocuvm(pgdir, newsz, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pgdir, (char*)a, PGSIZE, V2P(mem), PTE_W|PTE_U) < 0){
      cprintf("allocuvm out of memory (2)\n");
      deallocuvm(pgdir, newsz, oldsz);
      kfree(mem);
      return 0;
    }
    chgpgrefc(mem, 1);
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
int
deallocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  pte_t *pte;
  uint a, pa;

  if(newsz >= oldsz)
    return oldsz;

  a = PGROUNDUP(newsz);
  // cprintf("dealloc\n");
  for(; a  < oldsz; a += PGSIZE){
    pte = walkpgdir(pgdir, (char*)a, 0);
    if(!pte){
      // If pte doesn't exist
      a = PGADDR(PDX(a) + 1, 0, 0) - PGSIZE;
    }
    else if((*pte & PTE_P) != 0){
      char originkern=0;
      // Ptentry exists and is not present for this Pd
      if(getpgrefc(P2V((void*)(*pte)))){
        // Decrement if its still >0
        // Else it will be removed 
        // (edge case for when refcount already 0)
        chgpgrefc(P2V((void*)(*pte)), -1);
      }else{
        // somehow pgref is already 0 but we're here
        // must be case it was creating for initboot or 
        // kernel bootup use(??) do not decre allocpages
        // when case comes to show
        originkern=1;
      }
      if(getpgrefc(P2V((void*)(*pte)))==0){
        pa = PTE_ADDR(*pte); // get pa (PPN) of Pte
        // cprintf("deallocuvm: deallocing pte ppn %d\n", pa>>12);
        if(pa == 0){
          panic("kfree");
        }
        char *v = P2V(pa); // Convert pa PPN into va for kern
        kfree(v); // Free that 4KB pointed by PPN
        if(!originkern){
          allocpages--;
          // cprintf("deallocuvm: decrement allocp\n");
        }
        *pte = 0;
      }
    }
  }
  return newsz;
}

// Free a page table and all the physical memory pages
// in the user part.
void
freevm(pde_t *pgdir)
{
  uint i;

  if(pgdir == 0){
    panic("freevm: no pgdir");
  }
  deallocuvm(pgdir, KERNBASE, 0);
  for(i = 0; i < NPDENTRIES; i++){
    // For every Pd entry, free the contents (the whole Pt)
    if(pgdir[i] & PTE_P){
      char * v = P2V(PTE_ADDR(pgdir[i]));
      kfree(v);
    }
  }
  kfree((char*)pgdir);
}

// Clear PTE_U on a page. Used to create an inaccessible
// page beneath the user stack.
void
clearpteu(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if(pte == 0)
    panic("clearpteu");
  *pte &= ~PTE_U;
}

// Given a parent process's page table, create a copy
// of it for a child.
// Will be used to provide a deep clone or ref clone
pde_t*
copyuvm(pde_t *pgdir, uint sz, char deep)
{
  pde_t *d;
  pte_t *pte;
  uint pa, i, flags;
  char *mem;
  if(deep){
    if((d = setupkvm()) == 0) // Setup kernel's Pdirectory
      return 0;
    // Clone all of pgdir (Pdirectory), including even all
    // of Ptable entry pointer's pointed data
    // cprintf("copyuvm: deep\n");
    for(i = 0; i < sz; i += PGSIZE){
      // i+=PGSIZE increments middle 10bits in VA argument
      // aka will +1 the Ptable index
      // aka Pdirect index=0 (stay at illusion pde_t[0])
      // and at Ptable index=i/4096
      if((pte = walkpgdir(pgdir, (void *) i, 0)) == 0){
        // Check if pgdir exists (and don't allocate when nonexist)
        panic("copyuvm: pte should exist");
      }
      if(!(*pte & PTE_P)){
        panic("copyuvm: page not present");
      }
      // pte is virtual, accessing pte is the physical address
      pa = PTE_ADDR(*pte); // physical address, PPN of Ptentry
      flags = PTE_FLAGS(*pte) | PTE_W; // perms of entry
      if((mem = kalloc()) == 0){
        // Attempt to allocate 4KB of memory for pointed data
        // that's being pointed by pa (Ptable's PPN)
        goto bad;
      }
      // Move pointed data into newly allocated 4KB
      memmove(mem, (char*)P2V(pa), PGSIZE);
      // d: *pde, i: Pt index, mem: new 4KB addr ==> map mem copy
      if(mappages(d, (void*)i, PGSIZE, V2P(mem), flags) < 0){
        goto bad;
      }
      // cprintf("putting ppn %d into ppn %d\n", pa>>12, V2P(mem)>>12);
      // incr new Pte ref, decr the old Pte ref
      chgpgrefc(mem, 1);
      // cprintf("copyuvm: deep, increment allocp\n");
      allocpages++;
      // chgpgrefc(P2V(pa), -1); // decrement in dealloc instead
      // // Perhaps only 1 Pte has no ref but other has ref
      // // Just free that single Pte ==> may imply others will not have ref either
      // // Count number of Ptes to size, if equal then free pgdir instead
      // if(getpgrefc(P2V(pa))==0){
      //   cprintf("found refcount=0 pte, freeing it\n");
      //   *pte&=~PTE_P;
      //   kfree(P2V(pa));
      //   cprintf("copyuvm: deep, decrement allocp\n");
      //   allocpages--;
      // }
    }
  }else{
    // Reference copy
    d=pgdir;
    for(int i=0; i<sz; i+=PGSIZE){
      if((pte=walkpgdir(pgdir, (void*)i, 0))==0){
        panic("copyuvm: lite, pte should exist");
      }
      if(!(*pte&PTE_P)){
        panic("copuvm: lite page not present");
      }
      pa=PTE_ADDR(*pte);
      *pte&=~PTE_W;
      chgpgrefc(P2V(pa), 1);
    }
    lcr3(V2P(pgdir));
  }
  return d;

bad:
  freevm(d);
  return 0;
}

//PAGEBREAK!
// Map user virtual address to kernel address.
char*
uva2ka(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if((*pte & PTE_P) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  return (char*)P2V(PTE_ADDR(*pte));
}

// Copy len bytes from p to user address va in page table pgdir.
// Most useful when pgdir is not the current page table.
// uva2ka ensures this only works for PTE_U pages.
int
copyout(pde_t *pgdir, uint va, void *p, uint len)
{
  char *buf, *pa0;
  uint n, va0;

  buf = (char*)p;
  while(len > 0){
    va0 = (uint)PGROUNDDOWN(va);
    pa0 = uva2ka(pgdir, (char*)va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (va - va0);
    if(n > len)
      n = len;
    memmove(pa0 + (va - va0), buf, n);
    len -= n;
    buf += n;
    va = va0 + PGSIZE;
  }
  return 0;
}

void pgfaultintr(){
  // cprintf("pgfault start\n");
  struct proc *p=myproc();
  if(p==0){
    panic("pgfaultintr no proc on cpu");
  }
  // Only purpose of pgfault is to mem clone entire pgdir
  pde_t *oldpgdir=p->pgdir;
  if((p->pgdir=copyuvm(p->pgdir, p->sz, 1))==0){
    p->pgdir=oldpgdir;
    cprintf("failed to mem clone pgdir");
  }
  deallocuvm(oldpgdir, KERNBASE, 0);
  // cprintf("pgfault end\n");
}

//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.

