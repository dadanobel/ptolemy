/* Bare metal: the whole image lies in ROM from 0xFFF00000; the reset code copies .vec (to 0) and .text (to 0x3000) into RAM. */
SECTIONS {
  .rom 0xfff00000 : { *(.rom) }
  .vec 0 : AT(0xfff04000) { *(.vec) }
  .text 0x3000 : AT(0xfff07000) { *(.text) }
  ram_end = .;
}
