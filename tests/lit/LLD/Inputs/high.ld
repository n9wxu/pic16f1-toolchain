/* A variable 3000 bytes into linear RAM, beyond bank 31. */
SECTIONS
{
  .text 0x000200 : { *(.text) }
  .data 0x802BB8 : { *(.data) }
}
