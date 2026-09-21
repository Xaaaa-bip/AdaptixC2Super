package main

import (
	"crypto/aes"
	"crypto/cipher"
	"crypto/rand"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"math/bits"
)

// Session crypto for the gopher (32-byte key) path.
//
// Wire format (changed from double-RC4 to double-ChaCha20):
//   nonce(12) || XOR_sk( XOR_sub( AES-128-GCM(plaintext) ) )
//
// ChaCha20 uses the 128-bit key variant ("expand 16-byte k"), which lets us
// feed the existing 16-byte aesKey/subkey material directly. Every message
// gets a fresh random 96-bit nonce, so the outer keystreams are never
// reused — unlike the previous RC4 overlay, which replayed the same
// keystream per key and leaked XOR-of-ciphertexts.

// ---- ChaCha20 (16-byte key variant, 96-bit nonce) ----

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
		// columns
		chachaQr(&working[0], &working[4], &working[8], &working[12])
		chachaQr(&working[1], &working[5], &working[9], &working[13])
		chachaQr(&working[2], &working[6], &working[10], &working[14])
		chachaQr(&working[3], &working[7], &working[11], &working[15])
		// diagonals
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

// ---- Gopher transport overlay ----

func deriveGopherTransportSubkey(sk, salt []byte) []byte {
	out := make([]byte, 16)
	for i := 0; i < 16; i++ {
		out[i] = sk[i] ^ sk[(i+1)%16] ^ salt[i]
	}
	return out
}

func splitGopherSessionMaterial(key []byte) (aesKey, salt []byte, err error) {
	if len(key) >= 32 {
		return append([]byte(nil), key[:16]...), append([]byte(nil), key[16:32]...), nil
	}
	if len(key) == 16 {
		return key, make([]byte, 16), nil
	}
	return nil, nil, fmt.Errorf("session material: need at least 16 bytes, got %d", len(key))
}

// GopherSessionEncryptNogcm — ChaCha20-overlay-only encryption, matching
// the agent's gopher_decrypt_nogcm: [nonce 12B][chacha20(sub, nonce, data)].
// (The AES-GCM layer of the gopher scheme was dropped — the agent's
// hand-rolled AES-GCM hung on real-length inputs; the ChaCha20 overlay is
// the cross-language-verified primitive.)
func GopherSessionEncryptNogcm(plaintext, key []byte) ([]byte, error) {
	aesKey, salt, err := splitGopherSessionMaterial(key)
	if err != nil {
		return nil, err
	}
	cnonce := make([]byte, 12)
	if _, err = io.ReadFull(rand.Reader, cnonce); err != nil {
		return nil, err
	}
	sub := deriveGopherTransportSubkey(aesKey, salt)
	mid := chacha20Xor(sub, cnonce, plaintext)
	out := chacha20Xor(aesKey, cnonce, mid)

	res := make([]byte, 12+len(out))
	copy(res, cnonce)
	copy(res[12:], out)
	return res, nil
}

// GopherSessionDecryptNogcm — the inverse of GopherSessionEncryptNogcm,
// matching the agent's gopher_encrypt_nogcm output.
func GopherSessionDecryptNogcm(ciphertext, key []byte) ([]byte, error) {
	aesKey, salt, err := splitGopherSessionMaterial(key)
	if err != nil {
		return nil, err
	}
	if len(ciphertext) < 12 {
		return nil, errors.New("ciphertext too short")
	}
	cnonce := ciphertext[:12]
	body := ciphertext[12:]

	sub := deriveGopherTransportSubkey(aesKey, salt)
	mid := chacha20Xor(sub, cnonce, body)
	out := chacha20Xor(aesKey, cnonce, mid)
	return out, nil
}

// SessionEncrypt16 — AES-128-GCM for the 16-byte session keys (WS/HTTPS/
// LinuxTCP transports). Wire format: 12B nonce | ciphertext | 16B tag —
// matches the agent's aes128_gcm_encrypt exactly. The agent strictly
// decrypts every server response; without this the task stream never
// executes (the server previously sent plaintext msgpack).
func SessionEncrypt16(plaintext, key []byte) ([]byte, error) {
	if len(key) != 16 {
		return nil, errors.New("session key must be 16 bytes")
	}
	block, err := aes.NewCipher(key)
	if err != nil {
		return nil, err
	}
	gcm, err := cipher.NewGCM(block)
	if err != nil {
		return nil, err
	}
	nonce := make([]byte, gcm.NonceSize())
	if _, err = io.ReadFull(rand.Reader, nonce); err != nil {
		return nil, err
	}
	return gcm.Seal(nonce, nonce, plaintext, nil), nil
}

func GopherSessionEncrypt(plaintext, key []byte) ([]byte, error) {
	aesKey, salt, err := splitGopherSessionMaterial(key)
	if err != nil {
		return nil, err
	}
	block, err := aes.NewCipher(aesKey)
	if err != nil {
		return nil, err
	}
	gcm, err := cipher.NewGCM(block)
	if err != nil {
		return nil, err
	}
	nonce := make([]byte, gcm.NonceSize())
	if _, err = io.ReadFull(rand.Reader, nonce); err != nil {
		return nil, err
	}
	inner := gcm.Seal(nonce, nonce, plaintext, nil)

	// ChaCha20 double overlay with a fresh per-message nonce
	cnonce := make([]byte, 12)
	if _, err = io.ReadFull(rand.Reader, cnonce); err != nil {
		return nil, err
	}
	sub := deriveGopherTransportSubkey(aesKey, salt)
	mid := chacha20Xor(sub, cnonce, inner)
	out := chacha20Xor(aesKey, cnonce, mid)

	res := make([]byte, 12+len(out))
	copy(res, cnonce)
	copy(res[12:], out)
	return res, nil
}

func GopherSessionDecrypt(ciphertext, key []byte) ([]byte, error) {
	aesKey, salt, err := splitGopherSessionMaterial(key)
	if err != nil {
		return nil, err
	}
	if len(ciphertext) < 12 {
		return nil, errors.New("ciphertext too short")
	}
	cnonce := ciphertext[:12]
	body := ciphertext[12:]

	sub := deriveGopherTransportSubkey(aesKey, salt)
	mid := chacha20Xor(aesKey, cnonce, body)
	inner := chacha20Xor(sub, cnonce, mid)

	block, err := aes.NewCipher(aesKey)
	if err != nil {
		return nil, err
	}
	gcm, err := cipher.NewGCM(block)
	if err != nil {
		return nil, err
	}
	ns := gcm.NonceSize()
	if len(inner) < ns {
		return nil, errors.New("ciphertext too short")
	}
	nonce, ct := inner[:ns], inner[ns:]
	return gcm.Open(nil, nonce, ct, nil)
}
