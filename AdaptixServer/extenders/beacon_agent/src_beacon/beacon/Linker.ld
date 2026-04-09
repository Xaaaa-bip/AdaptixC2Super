/* Prepend MSVC-style .text$A / $B / $C into output .text via INSERT (keeps full MinGW PE script).
 * Do NOT pull *(.rdata*) here: MinGW puts .rdata$.refptr.* next to code for REL32; moving all .rdata*
 * to the start of .text breaks IMAGE_REL_AMD64_REL32 ("relocation truncated to fit").
 */
SECTIONS
{
  .text : {
    *(.text$A);
    *(.text$B);
    *(.text$C);
  }
} INSERT BEFORE .text;
