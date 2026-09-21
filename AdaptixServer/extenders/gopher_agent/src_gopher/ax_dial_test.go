package main

// 批次 2-β 判据5：回连拨号必须有上界（M8②/M8③ 同一个根）。
//
// 判据 = 「DialProfile 返回到失败的耗时」。
//   192.0.2.0/24 是 RFC 5737 保留给文档的网段，**保证不可路由** ⇒ SYN 得不到任何
//   应答 ⇒ 只能走满超时。不是"挑一个慢主机"，是不依赖外网状态的确定性黑洞。
//
// ★ 红对照：旧码 `net.Dial("tcp", addr)`（无超时），同一台机器上会拖到内核 SYN
//   重传的上界（约 130s）。这里用 30s 作分界线：绿侧 ~10s、红侧 ~130s，中间空
//   100s —— 不靠"紧贴阈值"来判别。
// ★ 下界（9s）同样重要：若某天 192.0.2.1 变成可路由/被防火墙立刻 REJECT，测试会
//   **立刻**返回错误而"轻松通过" —— 那是假绿。下界把这种情形变成显式的失败。

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/pem"
	"math/big"
	"testing"
	"time"

	"gopher/utils"
)

// selfSignedPEM 现场造一对自签证书。
// ★ 为什么非得真造：DialProfile 的 TLS 分支在拨号**之前**先做
//   `tls.X509KeyPair(profile.SslCert, profile.SslKey)` —— 证书为空时它直接
//   在这一步返回错误，**根本走不到拨号**，于是"有没有超时"就完全测不到
//   （判据会假绿：耗时 0s，"通过"了 <30s 的上界）。
func selfSignedPEM(t *testing.T) (certPEM, keyPEM []byte) {
	t.Helper()
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		t.Fatalf("生成密钥失败: %v", err)
	}
	tmpl := &x509.Certificate{
		SerialNumber:          big.NewInt(1),
		Subject:               pkix.Name{CommonName: "ax-m8-probe"},
		NotBefore:             time.Now().Add(-time.Hour),
		NotAfter:              time.Now().Add(time.Hour),
		KeyUsage:              x509.KeyUsageDigitalSignature | x509.KeyUsageCertSign,
		ExtKeyUsage:           []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth, x509.ExtKeyUsageClientAuth},
		BasicConstraintsValid: true,
		IsCA:                  true,
	}
	der, err := x509.CreateCertificate(rand.Reader, tmpl, tmpl, &key.PublicKey, key)
	if err != nil {
		t.Fatalf("签发证书失败: %v", err)
	}
	kb, err := x509.MarshalECPrivateKey(key)
	if err != nil {
		t.Fatalf("序列化私钥失败: %v", err)
	}
	return pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: der}),
		pem.EncodeToMemory(&pem.Block{Type: "EC PRIVATE KEY", Bytes: kb})
}

func TestDialProfileIsBounded(t *testing.T) {
	if testing.Short() {
		t.Skip("short 模式跳过（本测试要真的等一次超时）")
	}
	p := utils.Profile{Transport: "tcp"}
	t0 := time.Now()
	conn, err := DialProfile(p, "192.0.2.1:9")
	dt := time.Since(t0)

	if conn != nil {
		_ = conn.Close()
		t.Fatalf("192.0.2.1:9 竟然连上了（RFC 5737 不该可路由）⇒ 判据前提被破坏，本测试无效")
	}
	if err == nil {
		t.Fatal("应当返回错误")
	}
	t.Logf("拨号 %v 后失败: %v", dt.Round(time.Millisecond), err)

	if dt > 30*time.Second {
		t.Errorf("耗时 %v > 30s ⇒ 拨号没有上界（旧码约 130s）", dt.Round(time.Second))
	}
	if dt < 9*time.Second {
		t.Errorf("耗时 %v < 9s ⇒ 不是走满 10s 预算的超时路径（可能被立刻 REJECT），判据无判别力",
			dt.Round(time.Millisecond))
	}
}

// TestDialProfileTLSSharesTheBound：TLS 分支（tls.Dial）原先同样没有超时，
// 必须与明文分支吃同一个上界。★ 单独测它，是因为两条分支改法不同
// （net.Dial→net.DialTimeout vs tls.Dial→tls.DialWithDialer），只测一条会漏。
func TestDialProfileTLSSharesTheBound(t *testing.T) {
	if testing.Short() {
		t.Skip("short 模式跳过")
	}
	p := utils.Profile{UseSSL: true}
	p.SslCert, p.SslKey = selfSignedPEM(t)
	p.CaCert = p.SslCert
	t0 := time.Now()
	conn, err := DialProfile(p, "192.0.2.1:9")
	dt := time.Since(t0)

	if conn != nil {
		_ = conn.Close()
		t.Fatalf("TLS 分支竟然连上了不可路由地址")
	}
	if err == nil {
		t.Fatal("应当返回错误")
	}
	t.Logf("TLS 拨号 %v 后失败: %v", dt.Round(time.Millisecond), err)
	if dt > 30*time.Second {
		t.Errorf("TLS 分支耗时 %v > 30s ⇒ 没有上界", dt.Round(time.Second))
	}
	if dt < 9*time.Second {
		t.Errorf("TLS 分支耗时 %v < 9s ⇒ 不是超时路径", dt.Round(time.Millisecond))
	}
}
