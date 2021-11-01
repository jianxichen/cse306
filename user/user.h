struct stat;
struct rtcdate;
struct ptimes;

// system calls
int fork(void);
int exit(void) __attribute__((noreturn));
int wait(struct ptimes *ptime);
int pipe(int*);
int write(int, void*, int);
int read(int, void*, int);
int close(int);
int kill(int);
int exec(char*, char**);
int open(char*, int);
int mknod(char*, short, short);
int unlink(char*);
int fstat(int fd, struct stat*);
int link(char*, char*);
int mkdir(char*);
int chdir(char*);
int dup(int);
int getpid(void);
char* sbrk(int);
int sleep(int);
int uptime(void);
int readmouse(char*);
int sigsend(int pid, int sig);
int sigsethandler(int sig, void (*hand)(int sig));
void sigreturn(void);
int siggetmask(void);
int sigsetmask(int *maskp);
int sigpause(int mask);
int predict_cpu(int pretick); // hw 3 step 4
int sleeptick(int); // hw 3 step 5 (but sleep(int)) is already implemented in default xv6

// ulib.c
int stat(char*, struct stat*);
char* strcpy(char*, char*);
void *memmove(void*, void*, int);
char* strchr(const char*, char c);
int strcmp(const char*, const char*);
void printf(int, char*, ...);
char* gets(char*, int max);
uint strlen(char*);
void* memset(void*, int, uint);
void* malloc(uint);
void free(void*);
int atoi(const char*);
