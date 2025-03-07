#include "kernel/types.h"
#include "kernel/stat.h"
#include "user.h"

char buf[512];

void
cat(int fd)
{
  int n;
  printf(1, "debug: starting to cat read\n");
  while((n = read(fd, buf, sizeof(buf))) > 0) {
    // keep reading until EOF
    printf(1, "debug: cat reading\n");
    if (write(1, buf, n) != n) {
      printf(1, "cat: write error\n");
      exit();
    }
  }
  if(n < 0){
    printf(1, "cat: read error\n");
    exit();
  }
}

int
main(int argc, char *argv[])
{
  int fd, i;

  if(argc <= 1){
    cat(0);
    exit();
  }

  for(i = 1; i < argc; i++){
    if((fd = open(argv[i], 0)) < 0){
      printf(1, "cat: cannot open %s\n", argv[i]);
      exit();
    }
    cat(fd);
    close(fd);
  }
  exit();
}
