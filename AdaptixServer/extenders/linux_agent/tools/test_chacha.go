// test_chacha.go — standalone cross-language ChaCha20 checker (Go side).
// Usage: go run test_chacha.go <key16hex> <nonce12hex> <plainhex>
// Prints ciphertext hex.
//
// IMPORTANT: the chacha20 code below MUST stay byte-identical to
// session_crypto.go (chachaQr/chacha20Block/chacha20Xor). This standalone
// copy exists only because package-main plugin code cannot be imported.

package main

import (
	"encoding/binary"
	"encoding/hex"
	"fmt"
	"math/bits"
	"os"
)

func chachaQr(a, b, c, d *uint32) {
	*a += *b
	*d ^= *a
	*d = bits.RotateLeft32(*d, 16)
	*c += *d
	*b ^= *c
	*b = bits.RotateLeft32(*b, 12)
	*a += *b
	*d ^= *a
	*d = bits.RotateLeft32(*d, 8)
	*c += *d
	*b ^= *c
	*b = bits.RotateLeft32(*b, 7)
}

func chacha20Block(key [4]uint32, nonce [3]uint32, counter uint32) [64]byte {
	state := [16]uint32{
		0x61707865, 0x3320646e, 0x79622d32, 0x6b206574, // "expand 16-byte k"
		key[0], key[1], key[2], key[3],
		key[0], key[1], key[2], key[3],
		counter,
		nonce[0], nonce[1], nonce[2],
	}
	working := state
	for r := 0; r < 10; r++ {
		chachaQr(&working[0], &working[4], &working[8], &working[12])
		chachaQr(&working[1], &working[5], &working[9], &working[13])
		chachaQr(&working[2], &working[6], &working[10], &working[14])
		chachaQr(&working[3], &working[7], &working[11], &working[15])
		chachaQr(&working[0], &working[5], &working[10], &working[15])
		chachaQr(&working[1], &working[6], &working[11], &working[12])
		chachaQr(&working[2], &working[7], &working[8], &working[13])
		chachaQr(&working[3], &working[4], &working[9], &working[14])
	}
	var out [64]byte
	for i := 0; i < 16; i++ {
		binary.LittleEndian.PutUint32(out[4*i:], working[i]+state[i])
	}
	return out
}

func chacha20Xor(key16, nonce12, data []byte) []byte {
	if len(key16) != 16 || len(nonce12) != 12 {
		return nil
	}
	var key [4]uint32
	for i := 0; i < 4; i++ {
		key[i] = binary.LittleEndian.Uint32(key16[4*i:])
	}
	var nonce [3]uint32
	for i := 0; i < 3; i++ {
		nonce[i] = binary.LittleEndian.Uint32(nonce12[4*i:])
	}
	out := make([]byte, len(data))
	var counter uint32
	for off := 0; off < len(data); off += 64 {
		block := chacha20Block(key, nonce, counter)
		n := len(data) - off
		if n > 64 {
			n = 64
		}
		for i := 0; i < n; i++ {
			out[off+i] = data[off+i] ^ block[i]
		}
		counter++
	}
	return out
}

func main() {
	if len(os.Args) != 4 {
		fmt.Fprintln(os.Stderr, "usage: test_chacha <key16hex> <nonce12hex> <plainhex>")
		os.Exit(1)
	}
	key, err1 := hex.DecodeString(os.Args[1])
	nonce, err2 := hex.DecodeString(os.Args[2])
	data, err3 := hex.DecodeString(os.Args[3])
	if err1 != nil || err2 != nil || err3 != nil {
		fmt.Fprintln(os.Stderr, "bad hex input")
		os.Exit(2)
	}
	out := chacha20Xor(key, nonce, data)
	if out == nil {
		fmt.Fprintln(os.Stderr, "bad key/nonce length")
		os.Exit(3)
	}
	fmt.Print(hex.EncodeToString(out))
}
