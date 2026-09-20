// decrypt_diag.go — standalone session-decrypt diagnostic.
// Usage: go run decrypt_diag.go <key32hex> <datahex>
// Replicates GopherSessionDecrypt from session_crypto.go (byte-identical
// chacha + derive + split logic). Prints the plaintext or the failing layer.
package main

import (
	"crypto/aes"
	"crypto/cipher"
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
		0x61707865, 0x3320646e, 0x79622d32, 0x6b206574,
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

func deriveGopherTransportSubkey(sk, salt []byte) []byte {
	out := make([]byte, 16)
	for i := 0; i < 16; i++ {
		out[i] = sk[i] ^ sk[(i+1)%16] ^ salt[i]
	}
	return out
}

func main() {
	if len(os.Args) != 3 {
		fmt.Fprintln(os.Stderr, "usage: decrypt_diag <key32hex> <datahex>")
		os.Exit(1)
	}
	key, err := hex.DecodeString(os.Args[1])
	if err != nil || len(key) < 16 {
		fmt.Fprintln(os.Stderr, "bad key")
		os.Exit(2)
	}
	data, err := hex.DecodeString(os.Args[2])
	if err != nil {
		fmt.Fprintln(os.Stderr, "bad data")
		os.Exit(3)
	}

	var aesKey, salt []byte
	if len(key) >= 32 {
		aesKey = append([]byte(nil), key[:16]...)
		salt = append([]byte(nil), key[16:32]...)
	} else {
		aesKey = key
		salt = make([]byte, 16)
	}

	fmt.Printf("data_len=%d\n", len(data))
	if len(data) < 12 {
		fmt.Println("FAIL: data shorter than chacha nonce")
		os.Exit(4)
	}
	cnonce := data[:12]
	body := data[12:]
	fmt.Printf("cnonce=%x\n", cnonce)

	sub := deriveGopherTransportSubkey(aesKey, salt)
	mid := chacha20Xor(aesKey, cnonce, body)
	inner := chacha20Xor(sub, cnonce, mid)
	fmt.Printf("inner_len=%d inner_first16=%x\n", len(inner), inner[:min(16, len(inner))])

	block, err := aes.NewCipher(aesKey)
	if err != nil {
		fmt.Println("FAIL: aes key error:", err)
		os.Exit(5)
	}
	gcm, err := cipher.NewGCM(block)
	if err != nil {
		fmt.Println("FAIL: gcm error:", err)
		os.Exit(6)
	}
	ns := gcm.NonceSize()
	if len(inner) < ns {
		fmt.Println("FAIL: inner shorter than gcm nonce")
		os.Exit(7)
	}
	nonce, ct := inner[:ns], inner[ns:]
	pt, err := gcm.Open(nil, nonce, ct, nil)
	if err != nil {
		fmt.Println("FAIL: gcm.Open:", err, "(auth tag mismatch)")
		os.Exit(8)
	}
	fmt.Printf("OK plaintext(%d): %q\n", len(pt), pt)
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}
