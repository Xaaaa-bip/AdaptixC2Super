package main

import (
	"crypto/aes"
	"crypto/cipher"
	"crypto/rand"
	"crypto/rc4"
	"errors"
	"fmt"
	"io"
)

func deriveGopherTransportSubkey(sk, salt []byte) []byte {
	out := make([]byte, 16)
	for i := 0; i < 16; i++ {
		out[i] = sk[i] ^ sk[(i+1)%16] ^ salt[i]
	}
	return out
}

func gopherRC4Stream(data, key []byte) ([]byte, error) {
	c, err := rc4.NewCipher(key)
	if err != nil {
		return nil, err
	}
	out := make([]byte, len(data))
	c.XORKeyStream(out, data)
	return out, nil
}

func gopherTransportOverlay(buf []byte, sk, salt []byte, decrypt bool) ([]byte, error) {
	sub := deriveGopherTransportSubkey(sk, salt)
	if decrypt {
		x, err := gopherRC4Stream(buf, sub)
		if err != nil {
			return nil, err
		}
		return gopherRC4Stream(x, sk)
	}
	x, err := gopherRC4Stream(buf, sk)
	if err != nil {
		return nil, err
	}
	return gopherRC4Stream(x, sub)
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
	return gopherTransportOverlay(inner, aesKey, salt, false)
}

func GopherSessionDecrypt(ciphertext, key []byte) ([]byte, error) {
	aesKey, salt, err := splitGopherSessionMaterial(key)
	if err != nil {
		return nil, err
	}
	inner, err := gopherTransportOverlay(append([]byte(nil), ciphertext...), aesKey, salt, true)
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
