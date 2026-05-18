SECTIONS
{
  .text : {
    *(.text$A);
    *(.text$B);
    *(.text$C);
    *(.text*);
  }
  .rdata : {
    *(.rdata*);
    *(.rdata$*);
  }
  .data : {
    *(.data*);
  }
  .bss : {
    *(.bss*);
    *(COMMON);
  }
  .idata : {
    *(.idata*);
    *(.idata$*);
  }
  .rsrc : {
    *(.rsrc*);
    *(.rsrc$*);
  }
}
