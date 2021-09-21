/**
 * ML (stand for Mouse Listener), a program that prints the mouse events.
 */

#include "kernel/mouse.h"
#include "user.h"

#define STDIN (0)
#define STDOUT (1)
#define STDERR (2)

int main(int argc, char const *argv[])
{
  mpkt_t pkt;
  mflags_t *f = (mflags_t *)&pkt.flags;
  printf(STDOUT, "Hello Mouse!\n");
  while (1)
  {
    if (readmouse((char *)&pkt) == -1)
    {
      printf(STDOUT, "ERROR\n");
      return -1;
    }
    printf(STDERR, "%x %x %x = ", pkt.flags, pkt.x_movement, pkt.y_movement);
    printf(STDOUT, "%d%d %d%d %d %d%d%d -- %d %d ==> ", f->y_overflow, f->x_overflow, f->y_sign, f->x_sign, f->always1, f->middle_btn, f->right_btn, f->left_btn, pkt.x_movement, pkt.y_movement);
    if (f->left_btn)
      printf(STDOUT, "Left clicked, ");
    if (f->middle_btn)
      printf(STDOUT, "Middle clicked, ");
    if (f->right_btn)
      printf(STDOUT, "Right clicked, ");
    if (pkt.x_movement || pkt.y_movement)
    {
      int x = f->x_sign ? (0xFFFFFF00 | pkt.x_movement) : (pkt.x_movement);
      int y = f->y_sign ? (0xFFFFFF00 | pkt.y_movement) : (pkt.y_movement);
      printf(STDOUT, "Move (%d, %d)", x, y);
    }
    printf(STDOUT, "\n");
  }

  return 0;
}
