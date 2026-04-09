; Stage 2: PE entry for IAT-hidden EXE (x64). Stack aligned; shadow space for Win64.
[BITS 64]

global AxAsmEntry
extern AxRawExeEntry

section .text
AxAsmEntry:
	push rsi
	mov rsi, rsp
	and rsp, 0FFFFFFFFFFFFFFF0h
	sub rsp, 0x20
	xor ecx, ecx
	call AxRawExeEntry
	mov rsp, rsi
	pop rsi
	xor eax, eax
	ret
