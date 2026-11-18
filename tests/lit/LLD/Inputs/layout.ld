/* Fixed addresses so that the test can state every relocated value. */
SECTIONS
{
  .text   0x000200 : { *(.text) }
  .far    0x001A00 : { *(.far) }
  .data   0x802050 : { *(.data) }
  .rodata 0x808700 : { *(.rodata) }
}
