# The xv6 kernel starts executing in this file. This file is linked with
# the kernel C code, so it can refer to kernel symbols such as main().
# The boot block (bootasm.S and bootmain.c) jumps to entry below.
        
# Multiboot header, for multiboot boot loaders like GNU Grub.
# http://www.gnu.org/software/grub/manual/multiboot/multiboot.html
#
# Using GRUB 2, you can boot xv6 from a file stored in a
# Linux file system by copying kernel or kernelmemfs to /boot
# and then adding this menu entry:
#
# menuentry "xv6" {
# 	insmod ext2
# 	set root='(hd0,msdos1)'
# 	set kernel='/boot/kernel'
# 	echo "Loading ${kernel}..."
# 	multiboot ${kernel} ${kernel}
# 	boot
# }

#include "asm.h"
#include "memlayout.h"
#include "mmu.h"
#include "param.h"

# Multiboot header.  Data to direct multiboot loader.
.section .lomem, "ax"
.p2align 2
.globl multiboot_header
multiboot_header:
  #define magic 0x1badb002
  #define flags 0
  .long magic
  .long flags
  .long (-magic-flags)

# By convention, the _start symbol specifies the ELF entry point.
.globl _start
_start = entry

# Entering xv6 on boot processor, in 32-bit protected mode,
# but with paging off.
.globl entry
entry:
  # If we were loaded by the xv6 bootblock, then the segment registers
  # will have been set appropriately, but if we were loaded by some
  # multiboot-compliant loader (such as GRUB2), the segment registers will
  # likely not match how we are going to set up our GDT.  According to
  # the multiboot specification, CS is a 32-bit read/execute code segment
  # with an offset of '0' and a limit of '0xFFFFFFFF' and DS, ES, FS, GS, SS,
  # are 32-bit read/write data segment(s) with an offset of '0' and a
  # limit of '0xFFFFFFFF'.  There is not guaranteed to be any valid GDT,
  # and even if there was one we don't know what is in it.  So the first
  # thing we have to do is load our own GDT and then set the segment registers
  # to match.
  lgdt    gdtdesc

  movw	  $(SEG_KDATA<<3), %ax
  movw	  %ax, %ds
  movw	  %ax, %es
  movw	  %ax, %ss

  # We are not using %fs and %gs, so set them to 0.
  xorw	  %ax, %ax
  movw	  %ax, %fs
  movw	  %ax, %gs

  # Perform a long jmp to reload %cs.
  ljmp    $(SEG_KCODE<<3), $(cont)
cont:

  # Turn on page size extension for 4Mbyte pages
  movl    %cr4, %eax
  orl     $(CR4_PSE), %eax
  movl    %eax, %cr4
  # Set page directory
  movl    $(V2P_WO(entrypgdir)), %eax
  movl    %eax, %cr3
  # Turn on paging.
  movl    %cr0, %eax
  orl     $(CR0_PG|CR0_WP), %eax
  movl    %eax, %cr0

  # Set up the stack pointer.
  movl $(stack + KSTACKSIZE), %esp

  # Jump to main(), and switch to executing at
  # high addresses. The indirect call is needed because
  # the assembler produces a PC-relative instruction
  # for a direct jump.
  mov $main, %eax
  jmp *%eax

# Bootstrap GDT
.p2align 2                                # force 4 byte alignment
gdt:
  SEG_NULLASM                             # null seg
  SEG_ASM(STA_X|STA_R, 0x0, 0xffffffff)   # code seg
  SEG_ASM(STA_W, 0x0, 0xffffffff)         # data seg

gdtdesc:
  .word   (gdtdesc - gdt - 1)             # sizeof(gdt) - 1
  .long   gdt                             # address gdt

# Kernel stack
.comm stack, KSTACKSIZE
