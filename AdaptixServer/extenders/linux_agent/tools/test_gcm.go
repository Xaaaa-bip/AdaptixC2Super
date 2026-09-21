// test_gcm.go — Go side of the AES-128-GCM cross-language check.
// Uses the same deterministic nonce as test_gcm.c's stub so outputs
// can be compared byte-for-byte.
package main

import (
	"crypto/aes"
	"crypto/cipher"
	"encoding/hex"
	"fmt"
	"os"
	"os/exec"
	"strings"
)

var fixedNonce = []byte{0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b}

func mustDecode(s string) []byte {
	b, err := hex.DecodeString(strings.TrimSpace(s))
	if err != nil {
		panic(err)
	}
	return b
}

func gcmSeal(key, plain []byte) []byte {
	block, err := aes.NewCipher(key)
	if err != nil {
		panic(err)
	}
	gcm, err := cipher.NewGCM(block)
	if err != nil {
		panic(err)
	}
	// NOTE: use dst=fixedNonce (NOT nil) — the forked Go toolchain on this
	// box (go1.25.4, greenteagc fork) drops the nonce prefix when dst=nil.
	return gcm.Seal(fixedNonce, fixedNonce, plain, nil)
}

func gcmOpen(key, wire []byte) ([]byte, bool) {
	block, err := aes.NewCipher(key)
	if err != nil {
		panic(err)
	}
	gcm, err := cipher.NewGCM(block)
	if err != nil {
		panic(err)
	}
	if len(wire) < 12 {
		return nil, false
	}
	pt, err := gcm.Open(nil, wire[:12], wire[12:], nil)
	return pt, err == nil
}

func runC(args ...string) string {
	out, err := exec.Command("./test_gcm", args...).Output()
	if err != nil {
		fmt.Printf("C command failed: %v %v\n", args, err)
		os.Exit(2)
	}
	return strings.TrimSpace(string(out))
}

func main() {
	key := []byte("0123456789abcdef")

	if len(os.Args) > 1 {
		switch os.Args[1] {
		case "enc":
			pt := mustDecode(os.Args[3])
			fmt.Printf("%x\n", gcmSeal(mustDecode(os.Args[2]), pt))
			return
		case "dec":
			wire := mustDecode(os.Args[3])
			pt, ok := gcmOpen(mustDecode(os.Args[2]), wire)
			if !ok {
				fmt.Println("FAIL")
				os.Exit(1)
			}
			fmt.Printf("%x\n", pt)
			return
		}
	}

	lengths := []int{15, 16, 17, 31, 32, 33, 47, 100, 300, 1024, 4097, 10000}
	allOK := true
	for _, n := range lengths {
		// deterministic pseudo-plaintext: repeating pattern of all byte values
		plain := make([]byte, n)
		for i := range plain {
			plain[i] = byte((i*7 + n) & 0xff)
		}

		// Direction 1: C encrypt -> Go decrypt
		cWire := mustDecode(runC("enc", hex.EncodeToString(key), hex.EncodeToString(plain)))
		pt, ok := gcmOpen(key, cWire)
		d1 := ok && string(pt) == string(plain)

		// Direction 2: Go encrypt -> C decrypt
		goWire := gcmSeal(key, plain)
		cPlainHex := runC("dec", hex.EncodeToString(key), hex.EncodeToString(goWire))
		d2 := cPlainHex != "FAIL" && cPlainHex == hex.EncodeToString(plain)

		status := "PASS"
		if !(d1 && d2) {
			status = "FAIL"
			allOK = false
		}
		fmt.Printf("len=%6d  C->Go:%-5v  Go->C:%-5v  %s\n", n, d1, d2, status)
	}
	if !allOK {
		os.Exit(1)
	}
	fmt.Println("ALL PASS")
}
