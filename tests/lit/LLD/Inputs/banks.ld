/* .data starts at linear address 0x2050, the first byte of a bank; the
 * next bank boundary is at 0x20A0. */
SECTIONS
{
  .text   0x000200 : { *(.text) }
  .data   0x802050 : { *(.data .data.*) }
}
