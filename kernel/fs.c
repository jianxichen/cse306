// File system implementation.  Five layers:
//   + Blocks: allocator for raw disk blocks.
//   + Log: crash recovery for multi-step updates.
//   + Files: inode allocator, reading, writing, metadata.
//   + Directories: inode with special contents (list of other inodes!)
//   + Names: paths like /usr/rtm/xv6/fs.c for convenient naming.
//
// This file contains the low-level file system manipulation
// routines.  The (higher-level) system call implementations
// are in sysfile.c.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "semaphore.h"
#include "buf.h"
#include "file.h"
#include "proc.h"
#include "ufs.h"

#define min(a, b) ((a) < (b) ? (a) : (b))
static void itrunc(struct inode*);
// there should be one superblock per disk device, but we run with
// only one device
struct superblock sb;
extern struct unix_superb u_sb;

// Read the super block.
void
readsb(int dev, struct superblock *sb)
{
  struct buf *bp;

  bp = bread(dev, 1); // Buffer that has received contnts from block# on dev#
  memmove(sb, bp->data, sizeof(*sb));
  brelse(bp);
}

// Zero a block.
static void
bzero(int dev, int bno)
{
  struct buf *bp;

  bp = bread(dev, bno);
  memset(bp->data, 0, BSIZE);
  log_write(bp);
  brelse(bp);
}

// Blocks.

// Allocate a zeroed disk block.
static uint
balloc(uint dev){
  int b, bi, m;
  struct buf *bp;

  bp = 0;
  for(b = 0; b < sb.size; b += BPB){
    bp = bread(dev, BBLOCK(b, sb));
    for(bi = 0; bi < BPB && b + bi < sb.size; bi++){
      m = 1 << (bi % 8);
      if((bp->data[bi/8] & m) == 0){  // Is block free?
        bp->data[bi/8] |= m;  // Mark block in use.
        log_write(bp);
        brelse(bp);
        bzero(dev, b + bi);
        return b + bi;
      }
    }
    brelse(bp);
  }
  panic("balloc: out of blocks");
}

// Free a disk block.
static void
bfree(int dev, uint b)
{
  struct buf *bp;
  int bi, m;

  readsb(dev, &sb);
  bp = bread(dev, BBLOCK(b, sb));
  bi = b % BPB;
  m = 1 << (bi % 8);
  if((bp->data[bi/8] & m) == 0)
    panic("freeing free block");
  bp->data[bi/8] &= ~m;
  log_write(bp);
  brelse(bp);
}

// Inodes.
//
// An inode describes a single unnamed file.
// The inode disk structure holds metadata: the file's type,
// its size, the number of links referring to it, and the
// list of blocks holding the file's content.
//
// The inodes are laid out sequentially on disk at
// sb.startinode. Each inode has a number, indicating its
// position on the disk.
//
// The kernel keeps a cache of in-use inodes in memory
// to provide a place for synchronizing access
// to inodes used by multiple processes. The cached
// inodes include book-keeping information that is
// not stored on disk: ip->ref and ip->valid.
//
// An inode and its in-memory representation go through a
// sequence of states before they can be used by the
// rest of the file system code.
//
// * Allocation: an inode is allocated if its type (on disk)
//   is non-zero. ialloc() allocates, and iput() frees if
//   the reference and link counts have fallen to zero.
//
// * Referencing in cache: an entry in the inode cache
//   is free if ip->ref is zero. Otherwise ip->ref tracks
//   the number of in-memory pointers to the entry (open
//   files and current directories). iget() finds or
//   creates a cache entry and increments its ref; iput()
//   decrements ref.
//
// * Valid: the information (type, size, &c) in an inode
//   cache entry is only correct when ip->valid is 1.
//   ilock() reads the inode from
//   the disk and sets ip->valid, while iput() clears
//   ip->valid if ip->ref has fallen to zero.
//
// * Locked: file system code may only examine and modify
//   the information in an inode and its content if it
//   has first locked the inode.
//
// Thus a typical sequence is:
//   ip = iget(dev, inum)
//   ilock(ip)
//   ... examine and modify ip->xxx ...
//   iunlock(ip)
//   iput(ip)
//
// ilock() is separate from iget() so that system calls can
// get a long-term reference to an inode (as for an open file)
// and only lock it for short periods (e.g., in read()).
// The separation also helps avoid deadlock and races during
// pathname lookup. iget() increments ip->ref so that the inode
// stays cached and pointers to it remain valid.
//
// Many internal file system functions expect the caller to
// have locked the inodes involved; this lets callers create
// multi-step atomic operations.
//
// The icache.lock spin-lock protects the allocation of icache
// entries. Since ip->ref indicates whether an entry is free,
// and ip->dev and ip->inum indicate which i-node an entry
// holds, one must hold icache.lock while using any of those fields.
//
// An ip->lock sleep-lock protects all ip-> fields other than ref,
// dev, and inum.  One must hold ip->lock in order to
// read or write that inode's ip->valid, ip->size, ip->type, &c.

struct {
  struct spinlock lock;
  struct inode inode[NINODE];
} icache;

void
iinit(int dev)
{
  int i = 0;
  
  initlock(&icache.lock, "icache");
  for(i = 0; i < NINODE; i++) {
    initsleeplock(&icache.inode[i].lock, "inode");
  }

  readsb(dev, &sb);
  cprintf("sb: size %d nblocks %d ninodes %d nlog %d logstart %d\
          inodestart %d bmap start %d\n", sb.size, sb.nblocks,
          sb.ninodes, sb.nlog, sb.logstart, sb.inodestart,
          sb.bmapstart);
}

static struct inode* iget(uint dev, uint inum);

//PAGEBREAK!
// Allocate an inode on device dev.
// Mark it as allocated by giving it a type.
// Returns an unlocked but allocated and referenced inode.
struct inode*
ialloc(uint dev, short type)
{
  int inum;
  struct buf *bp;

  if(dev<2){
    struct dinode *dip;
    for(inum = 1; inum < sb.ninodes; inum++){
      bp = bread(dev, IBLOCK(inum, sb));
      dip = (struct dinode*)bp->data + inum%IPB;
      if(dip->type == 0){  // a free inode
        memset(dip, 0, sizeof(*dip));
        dip->type = type;
        log_write(bp);   // mark it allocated on the disk
        brelse(bp);
        return iget(dev, inum);
      }
      brelse(bp);
    }
  }else if(dev<4){
    struct unix_dinode *dip;
    // look for a free inode entry from Uv5 superblock
    for(int i=0; i<u_sb.s_ninode; i++){
      inum=u_sb.s_inode[i]; // iterating through free inode list
      bp=bread(dev, U_IBLOCK(inum, u_sb)); // get block where inode exists
      dip=(struct unix_dinode*)bp->data + (inum-1)%U_IPB;
      if((dip->i_mode&IALLOC)!=IALLOC){ // check if inode currently allocated
        memset(dip, 0, sizeof(*dip)); // zero out dip
        dip->i_mode=type|IALLOC; // Refer to file.h for Uv5<->Inode
        // log_write(bp); // mark it allocated on disk via log
        bp->flags|=B_DIRTY;
        bwrite(bp); // instantly write, ignore logging
        brelse(bp);
        return iget(dev, inum);
      }
      brelse(bp);
    }
  }
  panic("ialloc: no inodes");
}

// Copy a modified in-memory inode to disk.
// Must be called after every change to an ip->xxx field
// that lives on disk, since i-node cache is write-through.
// Caller must hold ip->lock.
void
iupdate(struct inode *ip)
{
  struct buf *bp;

  if(ip->dev<2){
    struct dinode *dip;
    bp = bread(ip->dev, IBLOCK(ip->inum, sb));
    dip = (struct dinode*)bp->data + ip->inum%IPB;
    dip->type = ip->type;
    dip->major = ip->major;
    dip->minor = ip->minor;
    dip->nlink = ip->nlink;
    dip->size = ip->size;
    memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
    log_write(bp);
    brelse(bp);
  }else if(ip->dev<4){
    struct unix_dinode *dip;
    bp=bread(ip->dev, U_IBLOCK(ip->inum, u_sb));
    dip=(struct unix_dinode*)bp->data + (ip->inum-1)%U_IPB;
    dip->i_mode=ip->type;
    dip->i_uid=ip->major;
    dip->i_gid=ip->minor;
    dip->i_nlink=ip->nlink;
    dip->i_size0=(ip->size>>16)&0xff;
    dip->i_size1=ip->size&0xffff;
    for(int i=0; i<8; i++){
      dip->i_addr[i]=ip->addrs[i];
    }
    bwrite(bp);
    brelse(bp);
  }
}

// Find the inode with number inum on device dev
// and return the in-memory copy. Does not lock
// the inode and does not read it from disk.
static struct inode*
iget(uint dev, uint inum) // just gives a free designated spot for inode
{
  struct inode *ip, *empty;

  acquire(&icache.lock);

  // Is the inode already cached?
  empty = 0;
  for(ip = &icache.inode[0]; ip < &icache.inode[NINODE]; ip++){
    if(ip->ref > 0 && ip->dev == dev && ip->inum == inum){
      ip->ref++;
      release(&icache.lock);
      return ip;
    }
    if(empty == 0 && ip->ref == 0)    // Remember empty slot.
      empty = ip;
  }

  // Recycle an inode cache entry.
  if(empty == 0)
    panic("iget: no inodes");

  ip = empty;
  ip->dev = dev;
  ip->inum = inum;
  ip->ref = 1;
  ip->valid = 0;
  release(&icache.lock);

  return ip;
}

// Increment reference count for ip.
// Returns ip to enable ip = idup(ip1) idiom.
struct inode*
idup(struct inode *ip)
{
  acquire(&icache.lock); // lock the inode cache table
  ip->ref++;
  release(&icache.lock);
  return ip;
}

// Lock the given inode.
// Reads the inode from disk if necessary.
void
ilock(struct inode *ip)
{
  struct buf *bp;

  if(ip == 0 || ip->ref < 1)
    panic("ilock");

  acquiresleep(&ip->lock);

  if(ip->valid == 0){
    if(ip->dev<2){
      // xv6 Disk inode
      bp = bread(ip->dev, IBLOCK(ip->inum, sb)); // retrieve block
      struct dinode *dip = (struct dinode*)bp->data + ip->inum%IPB; // offset cast
      // Convert (xv6) disk-inode to memory inode
      ip->type = dip->type;
      ip->major = dip->major;
      ip->minor = dip->minor;
      ip->nlink = dip->nlink;
      ip->size = dip->size;
      // Memmove addr[NDIRECT+1]
      memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
    }else{
      // Uv5 Disk inode
      bp=bread(ip->dev, U_IBLOCK(ip->inum, u_sb)); // retrieve sect
      struct unix_dinode *dip=(struct unix_dinode*)bp->data + (ip->inum-1)%U_IPB; // offset cast
      // Convert (Uv5) disk-inode to memory inode
      ip->type=dip->i_mode;
      ip->major=dip->i_uid;
      ip->minor=dip->i_gid;
      ip->nlink=dip->i_nlink;
      ip->size=dip->i_size0<<16 | dip->i_size1;
      // Move to addr[NDIRECT+1]
      int i;
      for(i=0; i<8; i++){
        ip->addrs[i]=dip->i_addr[i];
      }
      for(;i<NDIRECT+1; i++){
        ip->addrs[i]=0;
      }
    }
    brelse(bp);
    ip->valid = 1;
    if(ip->type == 0)    
      panic("ilock: no type");
  }
}

// Unlock the given inode.
void
iunlock(struct inode *ip)
{
  if(ip == 0 || !holdingsleep(&ip->lock) || ip->ref < 1)
    panic("iunlock");

  releasesleep(&ip->lock);
}

// Drop a reference to an in-memory inode.
// If that was the last reference, the inode cache entry can
// be recycled.
// If that was the last reference and the inode has no links
// to it, free the inode (and its content) on disk.
// All calls to iput() must be inside a transaction in
// case it has to free the inode.
void
iput(struct inode *ip)
{
  acquiresleep(&ip->lock);
  if(ip->valid && ip->nlink == 0){
    acquire(&icache.lock);
    int r = ip->ref;
    release(&icache.lock);
    if(r == 1){
      // inode has no links and no other references: truncate and free.
      itrunc(ip);
      ip->type = 0;
      iupdate(ip); // responsible to write a dirty inode onto disk 
      ip->valid = 0;
    }
  }
  releasesleep(&ip->lock);

  acquire(&icache.lock);
  ip->ref--;
  release(&icache.lock);
}

// Common idiom: unlock, then put.
void
iunlockput(struct inode *ip)
{
  iunlock(ip);
  iput(ip);
}

//PAGEBREAK!
// Inode content
//
// The content (data) associated with each inode is stored
// in blocks on the disk. The first NDIRECT block numbers
// are listed in ip->addrs[].  The next NINDIRECT blocks are
// listed in block ip->addrs[NDIRECT].
//
// Return the disk block address of the nth block in inode ip.
// If there is no such block, bmap allocates one.
// 
// bn is block number relative to the start of the file AKA index in addr[]
// if bn>NDIRECT, then use indirect reference to a block no containing 128 other blockno numbers
// Hence 12+128=140 block references
static uint
bmap(struct inode *ip, uint bn)
{
  uint addr, *a;
  struct buf *bp;

  if(bn < NDIRECT){ // if attempting to retrieve within NDIRECT
    if((addr = ip->addrs[bn]) == 0) // check if that ind is NULL
      ip->addrs[bn] = addr = balloc(ip->dev); // alloc block for it if NULL
    return addr; // return that block number
  }
  bn -= NDIRECT;

  if(bn < NINDIRECT){
    // Load indirect block, allocating if necessary.
    if((addr = ip->addrs[NDIRECT]) == 0)
      ip->addrs[NDIRECT] = addr = balloc(ip->dev); // alloc NULL block
    bp = bread(ip->dev, addr); 
    a = (uint*)bp->data; // uint* to char[512]
    if((addr = a[bn]) == 0){
      a[bn] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
    return addr;
  }

  panic("bmap: out of range");
}

// Truncate inode (discard contents).
// Only called when the inode has no links
// to it (no directory entries referring to it)
// and has no in-memory reference to it (is
// not an open file or current directory).
static void
itrunc(struct inode *ip)
{
  int i, j;
  struct buf *bp;
  uint *a;

  for(i = 0; i < NDIRECT; i++){
    if(ip->addrs[i]){
      bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }

  if(ip->addrs[NDIRECT]){
    bp = bread(ip->dev, ip->addrs[NDIRECT]);
    a = (uint*)bp->data;
    for(j = 0; j < NINDIRECT; j++){
      if(a[j])
        bfree(ip->dev, a[j]);
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT]);
    ip->addrs[NDIRECT] = 0;
  }

  ip->size = 0;
  iupdate(ip);
}

// Copy stat information from inode.
// Caller must hold ip->lock.
void
stati(struct inode *ip, struct stat *st)
{
  st->dev = ip->dev;
  st->ino = ip->inum;
  st->type = ip->type;
  st->nlink = ip->nlink;
  st->size = ip->size;
}

//PAGEBREAK!
// Read data from inode.
// Caller must hold ip->lock.
// uint n is # bytes we want to read
// If file/device, then we use fileread in devsw[]
// Otherwise we request buffer from disk
int
readi(struct inode *ip, char *dst, uint off, uint n){
  uint tot, m;
  struct buf *bp;
// cprintf("readi proc_pid %d dev:%d inode:%d\n", getppid(0), ip->dev, ip->inum); // what inode are reading from?
  if(ip->type == T_DEV){ // check if we're reading from external device, else file system
    if(ip->major < 0 || ip->major >= NDEV || !devsw[ip->major].read)
      return -1;
    return devsw[ip->major].read(ip, dst, n);
  }

  if(off > ip->size || off + n < off)
    return -1;
  if(off + n > ip->size)
    n = ip->size - off; // if n bytes will reach EOF, modify n

  if((ip->type&ILARG)==ILARG){
    // for Uv5 large (current implementation just singly)
    // each data blockno entry in indirect block is short type
    // will need to read indirect blockno first
    // there will be 256 short blockno# entries per indirect
    // https://www.seltzer.com/assets/oldhome/cs161.15/videos/fs-v6-audio.pdf
    // https://xiayingp.gitbook.io/build_a_os/labs/lab-8-file-system-large-files
    bp=bread(ip->dev, bmap(ip, off/(256*BSIZE)));
    unsigned short a=((short*)bp->data)[off/512];
    brelse(bp);
    for(tot=0; tot<n; tot+=m, off+=m, dst+=m){
      bp=bread(ip->dev, a);
      m = min(n - tot, BSIZE - off%BSIZE);
      memmove(dst, bp->data + off%BSIZE, m);
      brelse(bp);
    }
  }else{
    for(tot=0; tot<n; tot+=m, off+=m, dst+=m){
      bp = bread(ip->dev, bmap(ip, off/BSIZE)); // take offset from beginning of file to see what blocks we need to read
      m = min(n - tot, BSIZE - off%BSIZE);
      memmove(dst, bp->data + off%BSIZE, m); // copy out read operation from buffer cache object to destination buffer ==> read from disk to buffer object
      brelse(bp);
    }
  }
  return n;
}

// PAGEBREAK!
// Write data to inode.
// Caller must hold ip->lock.
int
writei(struct inode *ip, char *src, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(ip->type == T_DEV){
    if(ip->major < 0 || ip->major >= NDEV || !devsw[ip->major].write)
      return -1;
    return devsw[ip->major].write(ip, src, n);
  }

  if(off > ip->size || off + n < off)
    return -1;
  if(off + n > MAXFILE*BSIZE)
    return -1;

  for(tot=0; tot<n; tot+=m, off+=m, src+=m){
    bp = bread(ip->dev, bmap(ip, off/BSIZE));
    m = min(n - tot, BSIZE - off%BSIZE);
    memmove(bp->data + off%BSIZE, src, m);
    if(ip->dev<2){
      log_write(bp);
    }else{
      bwrite(bp);
    }
    brelse(bp);
  }

  if(n > 0 && off > ip->size){
    ip->size = off;
    iupdate(ip);
  }
  return n;
}

//PAGEBREAK!
// Directories

int
namecmp(const char *s, const char *t)
{
  return strncmp(s, t, DIRSIZ);
}

// Look for a directory entry in a directory.
// If found, set *poff to byte offset of entry.
struct inode*
dirlookup(struct inode *dp, char *name, uint *poff)
{
  uint off, inum;
  struct dirent de;

  if(dp->type != T_DIR && (dp->type&IFDIR)!=IFDIR)
    panic("dirlookup not DIR");
  // Read the directories within inode
  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de)) // uses readi to read through disk
      panic("dirlookup read"); // panic if what we read from inode !=sizeof(de)
    // buf->data from readi() cast into dirent after incl. offset
// cprintf("debug: dirlookup device: %d inode: %d name:%s\n", dp->dev, de.inum, de.name);
    if(de.inum == 0)
      continue;
    if(namecmp(name, de.name) == 0){
      // entry matches path element
      if(poff)
        *poff = off; // save offset into *poff
      inum = de.inum;
      return iget(dp->dev, inum); // create dummy inode to return
    }
  }

  return 0;
}

// Write a new directory entry (name, inum) into the directory dp.
int
dirlink(struct inode *dp, char *name, uint inum)
{
  int off;
  struct dirent de;
  struct inode *ip;

  // Check that name is not present.
  if((ip = dirlookup(dp, name, 0)) != 0){
    iput(ip);
    return -1;
  }

  // Look for an empty dirent.
  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlink read");
    if(de.inum == 0)
      break;
  }

  strncpy(de.name, name, DIRSIZ);
  de.inum = inum;
  if(writei(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
    panic("dirlink");

  return 0;
}

//PAGEBREAK!
// Paths

// Copy the next path element from path into name.
// Return a pointer to the element following the copied one.
// The returned path has no leading slashes,
// so the caller can check *path=='\0' to see if the name is the last one.
// If no name to remove, return 0.
//
// Examples:
//   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
//   skipelem("///a//bb", name) = "bb", setting name = "a"
//   skipelem("a", name) = "", setting name = "a"
//   skipelem("", name) = skipelem("////", name) = 0
//
static char*
skipelem(char *path, char *name)
{
  char *s;
  int len;

  while(*path == '/' || *path=='%') // pass through trailing '/' in pathname
    path++;
  if(*path == 0) // if after trailing '/' is just NULL
    return 0;
  s = path; // set the start of actual inode file traversing
  while(*path != '/' && *path != 0) // reach to end of inode name
    path++;
  len = path - s; // end - start = length of inode name
  if(len >= DIRSIZ)
    memmove(name, s, DIRSIZ); // move first 14 char of inode name
  else {
    memmove(name, s, len); // move sizeof len inode name
    name[len] = 0; // end name string with NULL
  }
  while(*path == '/') // pass more '/' to get to next inode name
    path++;
  return path; // next start point for inode pathname (or null)
}

// Look up and return the inode for a path name.
// If parent != 0, return the inode for the parent and copy the final
// path element into name, which must have room for DIRSIZ bytes.
// Must be called inside a transaction since it calls iput().
static struct inode*
namex(char *path, int nameiparent, char *name)
{
  struct inode *ip, *next;

  // Create a dummy inode (not read from disk)
  if(*path == '/') // if starts with '/', then work in the root directory, else cwd
    ip = iget(ROOTDEV, ROOTINO);
  else if(*path=='%')
    ip = iget(2, 1);
  else
    ip = idup(myproc()->cwd);

  while((path = skipelem(path, name)) != 0){
    ilock(ip); // reads given inode (*ip) from disk
    if(ip->type != T_DIR && (ip->type&IFDIR)!=IFDIR){ // if component not directory then return out
      iunlockput(ip);
      return 0;
    }
    if((ip->type&ILARG)==ILARG)
      cprintf("debug: namex this is ILARG file\n");
    if(nameiparent && *path == '\0'){
      // Stop one level early.
      iunlock(ip);
      return ip;
    }
// cprintf("debug: proc_pid %d namex in dev %d inode %d\n", getppid(0), ip->dev, ip->inum);
    if((next = dirlookup(ip, name, 0)) == 0){ // function that scans for directories in directory (dummy inode)
      iunlockput(ip); // did not find next inode
      return 0;
    }
    iunlockput(ip); // unlock current directory lock before attempting to get next lock for next dir ==> avoid deadlock
    ip = next;
  }
  if(nameiparent){
    iput(ip);
    return 0;
  }
  return ip;
}

struct inode*
namei(char *path)
{
  char name[DIRSIZ];
  return namex(path, 0, name);
}

struct inode*
nameiparent(char *path, char *name)
{
  return namex(path, 1, name);
}
