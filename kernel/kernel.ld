/* Simple linker script for the JOS kernel.
   See the GNU ld 'info' manual ("info ld") to learn the syntax. */

OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(_start)

SECTIONS
{
	/*
         * We have to put the entry code in its own segment,
	 * linked at the load adress, to avoid GRUB2 "entry point
         * isn't in a segment" error.
	 */
	. = 0x100000;
	.lomem : AT (0x100000) {
		*(.lomem)
	}

	/* Link the kernel at this address: "." means the current address */
        /* Must be equal to KERNLINK */
	. = ALIGN(0x10) + 0x80000000;

	/* Offset between load addresses and virtual addresses. */
        /* Must be equal to KERNBASE */
        KERNBASE = 0x80000000;

	.text : AT(ADDR(.text) - KERNBASE) {
		*(.text .stub .text.* .gnu.linkonce.t.*)
	}

	PROVIDE(etext = .);	/* Define the 'etext' symbol to this value */

	.rodata : AT(ADDR(.rodata) - KERNBASE) {
		*(.rodata .rodata.* .gnu.linkonce.r.*)
	}

	/* Include debugging information in kernel memory */
	.stab : AT(ADDR(.stab) - KERNBASE) {
		PROVIDE(__STAB_BEGIN__ = .);
		*(.stab);
		PROVIDE(__STAB_END__ = .);
		BYTE(0)		/* Force the linker to allocate space
				   for this section */
	}

	.stabstr : AT(ADDR(.stabstr) - KERNBASE) {
		PROVIDE(__STABSTR_BEGIN__ = .);
		*(.stabstr);
		PROVIDE(__STABSTR_END__ = .);
		BYTE(0)		/* Force the linker to allocate space
				   for this section */
	}

	/* Adjust the address for the data segment to the next page */
	. = ALIGN(0x1000);

	/* Conventionally, Unix linkers provide pseudo-symbols
	 * etext, edata, and end, at the end of the text, data, and bss.
	 * For the kernel mapping, we need the address at the beginning
	 * of the data section, but that's not one of the conventional
	 * symbols, because the convention started before there was a
	 * read-only rodata section between text and data. */
	PROVIDE(data = .);

	/* The data segment */
	.data : AT(ADDR(.data) - KERNBASE) {
		*(.data)
	}

	PROVIDE(edata = .);

	.bss : AT(ADDR(.bss) - KERNBASE) {
		*(.bss)
	}

	PROVIDE(end = .);

	/DISCARD/ : {
		*(.eh_frame .note.GNU-stack)
	}
}
