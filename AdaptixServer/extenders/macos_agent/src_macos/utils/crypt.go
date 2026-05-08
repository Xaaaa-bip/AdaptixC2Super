package utils

import (
	"crypto/aes"
	"crypto/cipher"
	"crypto/rand"
	"crypto/rc4"
	"fmt"
	"io"
)

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

// EncryptData: AES-GCM with first 16 bytes of key, then double-RC4 overlay (matches teamserver macos extender).
func EncryptData(data []byte, key []byte) ([]byte, error) {
	if len(key) < 32 {
		return nil, fmt.Errorf("key too short: need 32 bytes, got %d", len(key))
	}
	aesKey := key[:16]
	salt := key[16:32]

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
	if len(key) < 32 {
		return nil, fmt.Errorf("key too short: need 32 bytes, got %d", len(key))
	}
	aesKey := key[:16]
	salt := key[16:32]

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
		return nil, fmt.Errorf("ciphertext too short")
	}
	nonce, ct := inner[:ns], inner[ns:]
	return gcm.Open(nil, nonce, ct, nil)
}
