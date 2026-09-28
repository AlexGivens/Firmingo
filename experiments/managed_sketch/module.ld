/* One XIP-linked, stateless sketch module. The resident keeps boot2/USB/core. */
MEMORY {
  FLASH (rx) : ORIGIN = 0x10200100, LENGTH = 0x003fff00
}
SECTIONS {
  .text : {
    KEEP(*(.firmingo_entry.setup))
    KEEP(*(.firmingo_entry.loop))
    *(.text*)
    *(.rodata*)
    . = ALIGN(4);
  } > FLASH
  .data : { *(.data*) } > FLASH
  .bss (NOLOAD) : { *(.bss*) *(COMMON) } > FLASH
  ASSERT(SIZEOF(.data) == 0, "module has initialized mutable data")
  ASSERT(SIZEOF(.bss) == 0, "module has uninitialized mutable data")
  /DISCARD/ : { *(.comment*) *(.note*) *(.ARM.attributes*) *(.eh_frame*) }
}
