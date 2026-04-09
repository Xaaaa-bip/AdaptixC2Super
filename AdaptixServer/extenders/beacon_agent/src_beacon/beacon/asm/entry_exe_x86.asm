; Stage 2: PE entry for IAT-hidden EXE (x86)
[BITS 32]

global _AxAsmEntry
extern _AxRawExeEntry

section .text
_AxAsmEntry:
	push ebp
	mov ebp, esp
	and esp, 0FFFFFFF0h
	sub esp, 0x10
	call _AxRawExeEntry
	mov esp, ebp
	pop ebp
	xor eax, eax
	ret
