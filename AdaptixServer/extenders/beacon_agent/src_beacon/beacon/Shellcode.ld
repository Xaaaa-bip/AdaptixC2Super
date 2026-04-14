SECTIONS
{
  .text : {
    *(.text$A);        /* Entry point / ASM stub */
    *(.text$B);        /* Core code */
    *(.text$C);        /* Supporting functions */
    *(.text*);         /* Rest of code */
    *(.rdata*);        /* Read-only data */
    *(.data*);         /* Data */
    *(.bss*);          /* Zero-initialized data */
  }
  /DISCARD/ : {
    *(.comment)
    *(.note*)
    *(.eh_frame)
    *(.rel.text)
    *(.rel.data)
    *(.rel.rodata)
  }
}
