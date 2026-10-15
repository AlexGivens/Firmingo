/* Only the native stateful proof uses this linker script. RAM facts are supplied
 * by --defsym from a checked resident ELF; no guessed address is embedded here. */
MEMORY {
  FLASH (rx) : ORIGIN = 0x10200100, LENGTH = 3840
  STATE (rw) : ORIGIN = FIRMINGO_STATE_ADDRESS, LENGTH = FIRMINGO_STATE_BYTES
}
/* Exclude ELF file/program headers from flash payload. FMS2 supplies its own
 * 256-byte header; each initialized LOAD must match exactly one linked section. */
PHDRS {
  code PT_LOAD FLAGS(5);
  state PT_LOAD FLAGS(6);
}
SECTIONS {
  .text : {
    KEEP(*(.firmingo_entry.setup))
    KEEP(*(.firmingo_entry.loop))
    *(.text*) *(.rodata*)
    . = ALIGN(4);
  } > FLASH :code
  .data : {
    *(.data*)
    . = ALIGN(4);
  } > STATE AT> FLASH :state
  .bss (NOLOAD) : {
    *(.bss*) *(COMMON)
    . = ALIGN(4);
  } > STATE :state
  .unsupported : {
    KEEP(*(.preinit_array* .init_array* .fini_array* .ctors* .dtors*))
    KEEP(*(.eh_frame* .ARM.exidx* .ARM.extab* .tdata* .tbss*))
  } > FLASH :code
  ASSERT(SIZEOF(.unsupported) == 0, "constructors, unwind and TLS are unsupported")
  ASSERT(ADDR(.data) == ORIGIN(STATE), "data must begin at resident state base")
  ASSERT(ADDR(.bss) == ADDR(.data) + SIZEOF(.data), "BSS must follow data")
  /DISCARD/ : { *(.comment*) *(.note*) *(.ARM.attributes*) }
}
