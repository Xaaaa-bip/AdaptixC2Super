; Stage 2: PE entry for IAT-hidden service EXE (x64)
[BITS 64]

global AxAsmEntry
extern AxRawSvcEntry

section .text
AxAsmEntry:
	push rsi
	mov rsi, rsp
	and rsp, 0FFFFFFFFFFFFFFF0h
	sub rsp, 0x20
	call AxRawSvcEntry
	mov rsp, rsi
	pop rsi
	xor eax, eax
	ret
