/* MSVC-style PE sections for EXE/ServiceExe/DLL via INSERT AFTER/BEFORE.
 * Preserves MinGW default PE script while restructuring .text/.rdata/.data
 * to match typical MSVC output with proper section flags.
 *
 * .text  = RX only (no W)
 * .rdata = R only (no X)
 * .data  = RW only (no X)
 */

SECTIONS
{
  .text : {
    *(.text$A);
    *(.text$B);
    *(.text$C);
  }
} INSERT BEFORE .text;
