package main

import (
	"bytes"
	"crypto/rand"
	"encoding/hex"
	"os"
	"testing"
)

func TestChaCha20XorSymmetric(t *testing.T) {
	key := make([]byte, 16)
	nonce := make([]byte, 12)
	plain := make([]byte, 300) // spans multiple 64-byte blocks
	if _, err := rand.Read(key); err != nil {
		t.Fatal(err)
	}
	if _, err := rand.Read(nonce); err != nil {
		t.Fatal(err)
	}
	if _, err := rand.Read(plain); err != nil {
		t.Fatal(err)
	}

	ct := chacha20Xor(key, nonce, plain)
	if ct == nil {
		t.Fatal("chacha20Xor returned nil")
	}
	back := chacha20Xor(key, nonce, ct)
	if !bytes.Equal(back, plain) {
		t.Fatal("chacha20 roundtrip mismatch")
	}
	// Same inputs must produce the same keystream (determinism)
	if !bytes.Equal(chacha20Xor(key, nonce, plain), ct) {
		t.Fatal("chacha20 not deterministic")
	}
}

func TestGopherSessionRoundtrip(t *testing.T) {
	for _, keyLen := range []int{16, 32} {
		key := make([]byte, keyLen)
		if _, err := rand.Read(key); err != nil {
			t.Fatal(err)
		}
		for _, size := range []int{0, 1, 15, 16, 100, 4096} {
			plain := make([]byte, size)
			if _, err := rand.Read(plain); err != nil {
				t.Fatal(err)
			}
			ct, err := GopherSessionEncrypt(plain, key)
			if err != nil {
				t.Fatalf("encrypt k=%d s=%d: %v", keyLen, size, err)
			}
			// Wire format: 12-byte nonce prefix + AES-GCM + overlay
			if len(ct) != len(plain)+12+16+12 {
				t.Fatalf("ciphertext length: got %d, want %d", len(ct), len(plain)+40)
			}
			pt, err := GopherSessionDecrypt(ct, key)
			if err != nil {
				t.Fatalf("decrypt k=%d s=%d: %v", keyLen, size, err)
			}
			if !bytes.Equal(pt, plain) {
				t.Fatalf("roundtrip mismatch k=%d s=%d", keyLen, size)
			}
		}
	}
}

// TestDumpChaChaVector writes the plugin chacha20 output for externally
// provided inputs, so verify_linux_agent.sh can cross-check the C
// implementation (tools/test_chacha.c). Skipped unless all three env vars
// are set.
func TestDumpChaChaVector(t *testing.T) {
	keyHex := os.Getenv("CHACHA_VECTOR_KEY")
	nonceHex := os.Getenv("CHACHA_VECTOR_NONCE")
	plainHex := os.Getenv("CHACHA_VECTOR_PLAIN")
	outPath := os.Getenv("CHACHA_VECTOR_OUT")
	if keyHex == "" || nonceHex == "" || plainHex == "" || outPath == "" {
		t.Skip("cross-language vector env not set")
	}
	key, err := hex.DecodeString(keyHex)
	if err != nil {
		t.Fatal(err)
	}
	nonce, err := hex.DecodeString(nonceHex)
	if err != nil {
		t.Fatal(err)
	}
	plain, err := hex.DecodeString(plainHex)
	if err != nil {
		t.Fatal(err)
	}
	out := chacha20Xor(key, nonce, plain)
	if out == nil {
		t.Fatal("chacha20Xor returned nil")
	}
	if err := os.WriteFile(outPath, []byte(hex.EncodeToString(out)), 0644); err != nil {
		t.Fatal(err)
	}
}
