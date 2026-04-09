package utils

import (
	"crypto/aes"
	"crypto/cipher"
	"crypto/rand"
	"crypto/rc4"
	"errors"
	"fmt"
	"io"
)

// SKey is the full session material (16-byte AES key + 16-byte transport salt) after registration.
var SKey []byte

func deriveTransportSubkey(sk, salt []byte) []byte {
	out := make([]byte, 16)
	for i := 0; i < 16; i++ {
		out[i] = sk[i] ^ sk[(i+1)%16] ^ salt[i]
	}
	return out
}

func rc4Stream(data, key []byte) ([]byte, error) {
	c, err := rc4.NewCipher(key)
	if err != nil {
		return nil, err
	}
	out := make([]byte, len(data))
	c.XORKeyStream(out, data)
	return out, nil
}

func transportOverlay(buf []byte, sk, salt []byte, decrypt bool) ([]byte, error) {
	sub := deriveTransportSubkey(sk, salt)
	if decrypt {
		x, err := rc4Stream(buf, sub)
		if err != nil {
			return nil, err
		}
		return rc4Stream(x, sk)
	}
	x, err := rc4Stream(buf, sk)
	if err != nil {
		return nil, err
	}
	return rc4Stream(x, sub)
}

func splitSessionKeyMaterial(key []byte) (aesKey, salt []byte, err error) {
	if len(key) >= 32 {
		return append([]byte(nil), key[:16]...), append([]byte(nil), key[16:32]...), nil
	}
	if len(key) == 16 {
		return key, make([]byte, 16), nil
	}
	return nil, nil, fmt.Errorf("key too short: %d", len(key))
}

// EncryptData: AES-GCM with first 16 bytes of key, then double-RC4 overlay (matches teamserver gopher extender).
func EncryptData(data []byte, key []byte) ([]byte, error) {
	aesKey, salt, err := splitSessionKeyMaterial(key)
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
	inner := gcm.Seal(nonce, nonce, data, nil)
	return transportOverlay(inner, aesKey, salt, false)
}

// DecryptData inverts EncryptData.
func DecryptData(data []byte, key []byte) ([]byte, error) {
	aesKey, salt, err := splitSessionKeyMaterial(key)
	if err != nil {
		return nil, err
	}
	inner, err := transportOverlay(append([]byte(nil), data...), aesKey, salt, true)
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
	ns := gcm.NonceSize()
	if len(inner) < ns {
		return nil, errors.New("ciphertext too short")
	}
	nonce, ct := inner[:ns], inner[ns:]
	return gcm.Open(nil, nonce, ct, nil)
}
