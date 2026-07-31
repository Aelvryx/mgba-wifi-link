/* SPDX-License-Identifier: CC0-1.0 */
ENTRY(_start)

MEMORY {
	ROM (rx) : ORIGIN = 0x08000000, LENGTH = 256K
}

SECTIONS {
	. = ORIGIN(ROM);
	.text : {
		KEEP(*(.text.entry))
		*(.text*)
		*(.rodata*)
	} > ROM
	/DISCARD/ : {
		*(.comment)
		*(.ARM.attributes)
		*(.eh_frame*)
	}
}
