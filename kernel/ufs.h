/* flags */
#define ILOCK   01          /* inode is locked */
#define IUPD    02          /* inode has been modified */
#define IACC    04          /* inode access time to be updated */
#define IMOUNT  010         /* inode is mounted on */
#define IWANT   020         /* some process waiting on lock */
#define ITEXT   040         /* inode is pure text prototype */

/* modes */
#define IALLOC  0100000     /* file is used */
#define IFMT    060000      /* type of file */  
#define IFDIR   040000      /* directory */
#define IFCHR   020000      /* character special */
#define IFBLK   060000      /* block special, 0 is regular */
#define ILARG   010000      /* large addressing algorithm */
#define ISUID   04000       /* set user id on execution */
#define ISGID   02000       /* set group id on execution */
#define ISVTX   01000       /* save swapped text even after use */
#define IREAD   0400        /* read, write, execute permissions */
#define IWRITE  0200
#define IEXEC   0100

#define UNIX_INODES 100     // number found on unix 6th ed. site

// Unix disk-inode struct for conversion to xv6 mem inode
struct unix_dinode{
    unsigned short       i_mode;
    unsigned char        i_nlink;        /* directory entries */
    unsigned char        i_uid;          /* owner */
    unsigned char        i_gid;          /* group of owner */
    unsigned char        i_size0;        /* most significant of size */
    unsigned short       i_size1;        /* least sig of size*/ // char* --> short
    unsigned short       i_addr[8];      /* device addresses constituting file */
    unsigned char        i_flag;
    unsigned char        i_count;        /* reference count */
    unsigned short       i_dev;          /* device where inode resides */
    unsigned short       i_number;       /* i number, 1-to-1 with device address */
    unsigned short       i_lastr;        /* last logical block read (for read-ahead) */
};

struct unix_superb{
    unsigned short       s_isize;        /* number of sectors of inodes */
    unsigned short       s_fsize;        /* total disk sectors in the filesystem */
    unsigned short       s_nfree;        /* number of valid entries in s_free array */
    unsigned short       s_free[100];    /* holds sector numbers of free sectors */
    unsigned short       s_ninode;       /* number of valid entries in s_inode array */
    unsigned short       s_inode[100];   /* holds the indices of free inodes */
    unsigned char        s_flock;        /* lock during free list manipulation */
    unsigned char        s_ilock;        /* lock during I list manipulation */
    unsigned char        s_fmod;         /* super block modified flag */
    unsigned char        s_ronly;        /* mounted read-only flag */
    unsigned short       s_time[2];      /* current date of last update */
    // short pad[50];
};

#define U_BSIZE 512 // Uv5 sector size
#define U_SECTORs 2 // Uv5 inode starts at sector 2

// Uv5 Inodes per block(sector)
#define U_IPB (U_BSIZE / sizeof(struct unix_dinode))

// Uv5 Block(Sector) containing Uv5 inode i
#define U_IBLOCK(i, sb)     ((i-1) / U_IPB + 2)