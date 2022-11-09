OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)

IMAGE_START = 0x70008000; IMAGE_SIZE = 0x2000;

ENTRY(_start)
SECTIONS {
	. = IMAGE_START; __image_start = .;
	.text : {
		*(.text._start) *(.text .text.*)
		*(.rodata .rodata.* .gnu.linkonce.r.*)
		*(.data .data.* .gnu.linkonce.d.*)
	}
	.bss : {
		*(.bss .bss.* .gnu.linkonce.b.*)
	}
	__image_end = .;

	ASSERT(__image_end - IMAGE_START <= IMAGE_SIZE, "image overflow")

	.junk : { *(.shstrtab) }

	/DISCARD/ : { *(*) }
}
