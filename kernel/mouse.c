#include "mouse.h"
#include "x86.h"
#include "defs.h"
#include "traps.h"
#include "spinlock.h"
#include "sleeplock.h"

#define PSTAT (0x64)
#define PDATA (0x60)

#define BIT0 (0x01)
#define BIT1 (0x02)
#define BIT3 (0x08)
#define BIT5 (0x20)

#define ACK (0xFA)

#ifndef ISSET
  #define ISSET
  #define IS_SET(stat, bit) ((stat) & (bit))
#endif
#ifndef ISCLEAR
  #define ISCLEAR
  #define IS_CLEAR(stat, bit) (!((stat) & (bit)))
#endif

#define BUFLEN 129


static struct spinlock mouse_lock;
static int read=0, write=0, size=0;
static uint circlebuf[BUFLEN];
static struct sleeplock readsleep;

static void write_buffer(uint data){
  if(size<129){
      circlebuf[write]=data;
      write = (write+1)%BUFLEN;
      size++;
    } else {
      // cprintf("buffer is full\n");
    }
}

static int read_buffer(){
  // Under assumption that we will never reach datavalue > MAX(int)
  if(size>0){
    int out=circlebuf[read];
    read=(read+1)%BUFLEN;
    size--;
    return out;
  }
  // cprintf("buffer empty\n");
  return -1;
}

static void wait_read()
{
  uint timeout = 10000;
  uchar status = 0;
  while (--timeout) {
    status = inb(PSTAT);
    if (IS_SET(status, BIT0)) {
      // cprintf("Readable\n");
      return;
    }
  }
  cprintf("wait for reading - timeout\n");
}

static void wait_write()
{
  uint timeout = 10000;
  uchar status = 0;
  while (--timeout) {
    status = inb(PSTAT);
    if (IS_CLEAR(status, BIT1)) {
      // cprintf("Writable\n");
      return;
    }
  }
  cprintf("wait for writing - timeout\n");
}

static void wait_ack()
{
  uint timeout = 10000;
  while (--timeout) {
    if (inb(PDATA) == ACK) {
      // cprintf("ACK'ed!\n");
      return;
    }
  };
  cprintf("Wait for ACK - timeout\n");
}

void mouseinit(void)
{
  wait_write();
  outb(PSTAT, 0x20); // send the command byte 0x20 ("Get Compaq Status Byte") to the PS/2 controler on port 0x64
  wait_read();
  uint st = inb(PDATA); // returned status byte
  // cprintf("returned status byte = %d\n", st);

  st |= BIT1;    // Set Bit 1 (value = 2, "enable mouse interrupts")
  st &= ~(BIT5); // Clear Bit 5 (value = 0, "disable mouse clock")
  // cprintf("new status byte = %d\n", st);

  wait_write();
  outb(PSTAT, 0x60); // "Set Compaq Status"
  // wait_write();
  // outb(PSTAT, 0xD4); // !!! Sending a command or data to the mouse (0x60) must be preceded by sending a 0xD4 to port 0x64
  wait_write();
  outb(PDATA, st);   // Update the modified status byte

  // Setting new status byte [might] generate a 0xFA ACK byte from the keyboard that must be waited for and consumed.
  // wait_ack(); // NO??

  wait_write();
  outb(PSTAT, 0xD4);  // !!! Preceding Byte
  wait_write();
  outb(PDATA, 0xF4);  // Enable Packet Streaming
  wait_ack();

  initlock(&mouse_lock, "mouse"); //lock mouse writing
  initsleeplock(&readsleep, "read buf");
  ioapicenable(IRQ_MOUSE, 0);
}

void mouseintr(void){

  acquire(&mouse_lock);

  if ((inb(PSTAT) & BIT0) == 0) { // before reading data from 0x60, verify that bit 0 (value = 0x1) is set.
    goto end;
  }

  mpkt_t pkt;
  wait_read();
  pkt.flags = inb(PDATA);
  wait_read();
  pkt.x_movement = inb(PDATA);
  wait_read();
  pkt.y_movement = inb(PDATA);
  mflags_t* f = (mflags_t*)&pkt.flags;
  if (f->y_overflow || f->x_overflow) {
    cprintf("ERROR: Y or X overflow bits are set.\n");
    goto discard;
  }
  if (!f->always1) {
    cprintf("ERROR: bit 3 is not 1\n");
    goto discard;
  }

  write_buffer(pkt.flags);
  write_buffer(pkt.x_movement);
  write_buffer(pkt.y_movement);

discard:
  while (inb(PSTAT) & BIT0) {
    inb(PDATA); // discard bytes
  }

end:
  release(&mouse_lock);
  releasesleep(&readsleep);
}

static int readmouse(char* pkt){
  while(size<3){
    acquiresleep(&readsleep);
  }
  for(int i=0; i<3; i++){
    pkt[i]=read_buffer();
  }
  return 0;
}

int sys_readmouse(void){
  char *pkt;
  if(argptr(0, &pkt, 3)==-1){
    return -1;
  }
  return readmouse(pkt);
}
