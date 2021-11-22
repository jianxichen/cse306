#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "kernel/ufs.h"
#include "user.h"

char*
fmtname(char *path)
{
  static char buf[DIRSIZ+1];
  char *p;

  // Find first character after last slash.
  for(p=path+strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  // Return blank-padded name.
  if(strlen(p) >= DIRSIZ)
    return p;
  memmove(buf, p, strlen(p));
  memset(buf+strlen(p), ' ', DIRSIZ-strlen(p));
  return buf;
}

void
ls(char *path)
{
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

// printf(1, "opened\n");
  if((fd = open(path, 0)) < 0){
    printf(2, "ls: cannot open %s\n", path);
    return;
  }
// printf(1, "fstated 1\n");
  if(fstat(fd, &st) < 0){
    printf(2, "ls: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch(st.type){
  case T_FILE:
    printf(1, "%s %d %d %d\n", fmtname(path), st.type, st.ino, st.size);
    break;

  case T_DIR:
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
      printf(1, "ls: path too long\n");
      break;
    }
    strcpy(buf, path);
    p = buf+strlen(buf);
    *p++ = '/';
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      // read through each directory entry and call fstat on them
      if(de.inum == 0)
        continue;
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;
      if(stat(buf, &st) < 0){ // calls fstat
        printf(1, "ls: cannot stat %s\n", buf);
        continue;
      }
// if((st.type&ILARG)==ILARG){
// printf(1, "debug: ls this is ILARG file ");
// }
      printf(1, "%s %d %d %d\n", fmtname(buf), st.type, st.ino, st.size);
    }
    break;
  }
  // Top part is for xv6, this is for Uv5
  if(st.dev>1){
    if((st.type&IFDIR)==IFDIR){
      if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
        printf(1, "ls: path too long\n");
      }else{
        strcpy(buf, path);
        p = buf+strlen(buf); // put '/' at end
        *p++ = '/';
// printf(1, "buf name %s\n", buf);
// printf(1, "path name %s\n", buf);
// printf(1, "readin uv5\n");
        while(read(fd, &de, sizeof(de)) == sizeof(de)){
          // for each directory entry, use fstat on it
          if(de.inum == 0)
            continue;
          memmove(p, de.name, DIRSIZ);
          p[DIRSIZ] = 0;
// printf(1, "stat->fstated 2\n");
          if(stat(buf, &st) < 0){
            printf(1, "ls: cannot stat %s\n", buf);
            continue;
          }
          // Extract permission from Uv5 i_mode
          printf(1, "%s i_mode:%d inode:%d size:%d\n", fmtname(buf), (unsigned short)st.type, st.ino, st.size);
        }
      }
    }else{
      printf(1, "%s %d %d %d\n", fmtname(path), st.type, st.ino, st.size);
    }
  }
  close(fd);
}

int
main(int argc, char *argv[])
{
  int i;

  if(argc < 2){
    ls(".");
    exit();
  }
  for(i=1; i<argc; i++)
    ls(argv[i]);
  exit();
}
