#include "types.h"
#include "x86.h"
#include "defs.h"
#include "traps.h"
#include "spinlock.h"

#define PSTAT (0x64)
#define PDATA (0x60)

#define BIT0 (0x01)
#define BIT1 (0x02)
#define BIT5 (0x20)

#define ACK (0xFA)

#define IS_SET(stat, bit) ((stat) & (bit))
#define IS_CLEAR(stat, bit) (!((stat) & (bit)))

#define BUFLEN 129


static struct spinlock mouse_lock;
static int read=0, write=0, size=0;
static uint circlebuf[BUFLEN];

static void write_buffer(uint data){
  if(size<129){
      circlebuf[write]=data;
      write+=1%BUFLEN;
      size++;
    }
}

static int read_buffer(){
  // Under assumption that we will never reach datavalue > MAX(int)
  if(size>0){
    int out=circlebuf[read];
    read+=1%BUFLEN;
    size--;
    return out;
  }
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
  cprintf("returned status byte = %d\n", st);

  st |= BIT1;    // Set Bit 1 (value = 2, "enable mouse interrupts")
  st &= ~(BIT5); // Clear Bit 5 (value = 0, "disable mouse clock")
  cprintf("new status byte = %d\n", st);

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

  initlock(&mouse_lock, "mouse");
  ioapicenable(IRQ_MOUSE, 0);
}

void mouseintr(void){
  cprintf("Here is - mouse intr\n");

  acquire(&mouse_lock);

  uint st = inb(PSTAT);
  uint data_buf;
  if ((st & BIT0) == 0) { // before reading data from 0x60, verify that bit 0 (value = 0x1) is set.
    release(&mouse_lock);
    return;
  }
  while (st & BIT0) {
    data_buf = inb(PDATA);
    cprintf("st = %d  ", st);
    if (st & 0x20) {  // bit 5 is set ==> mouse
      cprintf("mouse event - %d\n", data_buf);
      // put mouse data in buffer
      write_buffer(data_buf);
      data_buf = 0;
    }
    // else {  // bit 5 is clear ==> keyboard
    //   cprintf("keyboard event\n");
    // }
    st = inb(PSTAT);
  }
  release(&mouse_lock);
}
