struct file {
  enum { FD_NONE, FD_PIPE, FD_INODE } type;
  int ref; // reference count
  char readable;
  char writable;
  struct pipe *pipe;
  struct inode *ip;
  uint off;
};


// in-memory copy of an inode
struct inode {
  uint dev;               // Device number
  uint inum;              // Inode number
  int ref;                // Reference count
  struct sleeplock lock;  // protects everything below here
  int valid;              // inode has been read from disk?
  // Hold copy of disk inode (xv6 is smooth cast)
  short type;             // Uv5: [int~>short] i_mode
  short major;            // Uv5: [char] i_uid
  short minor;            // Uv5: [char] i_gid
  short nlink;            // Uv5: [char] i_nlink
  uint size;              // Uv5: [char] i_size_0<<16 | [char*] i_size1
  uint addrs[NDIRECT+1];  // xv6: 7(d)+1(id); Uv5: S8(d), L8(id)
};

// table mapping major device number to
// device functions
struct devsw {
  int (*read)(struct inode*, char*, int);
  int (*write)(struct inode*, char*, int);
};

extern struct devsw devsw[];

#define CONSOLE 1
