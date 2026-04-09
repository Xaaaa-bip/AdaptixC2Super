// Package tlscert generates self-signed TLS certificates for development or lab teamservers.
// Browsers will still show warnings unless you add the cert to trust stores or use a real CA.
package tlscert

import (
	"crypto/rand"
	"crypto/rsa"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/hex"
	"encoding/pem"
	"errors"
	"fmt"
	"math/big"
	"net"
	"os"
	"path/filepath"
	"strings"
	"time"
)

const defaultRSABits = 2048

// Plausible generic defaults (no third-party trademarks). Override via profile tls_autogen_* fields or env ADAPTIX_TLS_AUTOGEN_CN.
var (
	autogenCNPatterns = []string{
		"www.cdn-%s.edge-gateway.net",
		"api.%s.static-delivery.net",
		"gateway-%s.https-accelerate.net",
		"origin-%s.traffic-route.net",
		"edge-%s.cdn-platform.net",
		"static-%s.asset-delivery.net",
	}
	autogenOrganizations = []string{
		"Cloud Networking Technology Co., Ltd.",
		"Digital Infrastructure Holdings",
		"Application Delivery Services Inc.",
		"Internet Traffic Engineering Ltd.",
		"Hosted Platform Solutions",
		"Global Edge Computing Corp",
		"Enterprise Web Services Group",
	}
	autogenOrgUnits = []string{
		"IT Operations",
		"Public HTTPS Services",
		"API Gateway Division",
		"Web Acceleration Team",
		"Security and Compliance",
		"Platform Engineering",
	}
	autogenCountries = []string{"US", "CN", "SG", "DE", "GB", "JP"}
)

func pickPoolIndex(n int) (int, error) {
	if n <= 0 {
		return 0, errors.New("empty pool")
	}
	b := make([]byte, 1)
	if _, err := rand.Read(b); err != nil {
		return 0, err
	}
	return int(b[0]) % n, nil
}

// CertSummary is parsed PEM certificate metadata for startup logs.
type CertSummary struct {
	CommonName         string
	Organization       string
	OrganizationalUnit string
	Country            string
	Locality           string
	Province           string
	DNSNames           []string
	IPAddresses        []string
	NotBefore          time.Time
	NotAfter           time.Time
	SerialNumberHex    string
	IsCA               bool
}

// ReadCertSummary parses the first certificate in a PEM file (Teamserver.cert).
func ReadCertSummary(certPath string) (*CertSummary, error) {
	pemData, err := os.ReadFile(certPath)
	if err != nil {
		return nil, err
	}
	var der []byte
	for {
		var block *pem.Block
		block, pemData = pem.Decode(pemData)
		if block == nil {
			break
		}
		if block.Type == "CERTIFICATE" {
			der = block.Bytes
			break
		}
	}
	if len(der) == 0 {
		return nil, errors.New("no PEM certificate block found")
	}
	cert, err := x509.ParseCertificate(der)
	if err != nil {
		return nil, err
	}
	sub := cert.Subject
	s := &CertSummary{
		CommonName:         sub.CommonName,
		Organization:       joinPKIX(sub.Organization),
		OrganizationalUnit: joinPKIX(sub.OrganizationalUnit),
		Country:            joinPKIX(sub.Country),
		Locality:           joinPKIX(sub.Locality),
		Province:           joinPKIX(sub.Province),
		DNSNames:           append([]string(nil), cert.DNSNames...),
		NotBefore:          cert.NotBefore,
		NotAfter:           cert.NotAfter,
		SerialNumberHex:    strings.TrimLeft(strings.ToUpper(cert.SerialNumber.Text(16)), "0"),
		IsCA:               cert.IsCA,
	}
	if s.SerialNumberHex == "" {
		s.SerialNumberHex = "0"
	}
	for _, ip := range cert.IPAddresses {
		if ip != nil {
			s.IPAddresses = append(s.IPAddresses, ip.String())
		}
	}
	return s, nil
}

func joinPKIX(parts []string) string {
	if len(parts) == 0 {
		return ""
	}
	return strings.Join(parts, " / ")
}

// DisguiseOptions controls X.509 subject and SANs. Empty fields get random generic corporate / CDN-style defaults (no .example). Set CN/O/OU/C for your own hostname and legal entity.
type DisguiseOptions struct {
	CommonName         string
	Organization       string
	OrganizationalUnit string
	Locality           string
	Province           string
	Country            string // ISO 3166-1 alpha-2, e.g. US
	DNSNames           []string
	IPAddresses        []net.IP
	ValidDays          int
}

// WriteSelfSignedRSA creates a PEM-encoded RSA key and self-signed certificate at the given paths.
// On success, info reflects the effective subject/SANs actually written (defaults applied).
func WriteSelfSignedRSA(certPath, keyPath string, opt DisguiseOptions) (*CertSummary, error) {
	if certPath == "" || keyPath == "" {
		return nil, errors.New("cert and key paths are required")
	}
	days := opt.ValidDays
	if days < 30 {
		days = 397
	}
	if days > 825 {
		days = 825
	}

	cn := opt.CommonName
	if cn == "" {
		suf := make([]byte, 4)
		if _, err := rand.Read(suf); err != nil {
			return nil, err
		}
		hexID := hex.EncodeToString(suf)
		pi, err := pickPoolIndex(len(autogenCNPatterns))
		if err != nil {
			return nil, err
		}
		cn = fmt.Sprintf(autogenCNPatterns[pi], hexID)
	}

	org := opt.Organization
	if org == "" {
		oi, err := pickPoolIndex(len(autogenOrganizations))
		if err != nil {
			return nil, err
		}
		org = autogenOrganizations[oi]
	}
	ou := opt.OrganizationalUnit
	if ou == "" {
		ui, err := pickPoolIndex(len(autogenOrgUnits))
		if err != nil {
			return nil, err
		}
		ou = autogenOrgUnits[ui]
	}
	country := opt.Country
	if country == "" {
		ci, err := pickPoolIndex(len(autogenCountries))
		if err != nil {
			return nil, err
		}
		country = autogenCountries[ci]
	}

	priv, err := rsa.GenerateKey(rand.Reader, defaultRSABits)
	if err != nil {
		return nil, fmt.Errorf("rsa key: %w", err)
	}

	serialLimit := new(big.Int).Lsh(big.NewInt(1), 128)
	serial, err := rand.Int(rand.Reader, serialLimit)
	if err != nil {
		return nil, err
	}

	notBefore := time.Now().Add(-1 * time.Hour)
	notAfter := notBefore.Add(time.Duration(days) * 24 * time.Hour)

	dns := append([]string(nil), opt.DNSNames...)
	dnsSeen := make(map[string]struct{})
	addDNS := func(s string) {
		if s == "" {
			return
		}
		if _, ok := dnsSeen[s]; ok {
			return
		}
		dnsSeen[s] = struct{}{}
		dns = append(dns, s)
	}
	addDNS(cn)
	addDNS("localhost")

	ips := append([]net.IP(nil), opt.IPAddresses...)

	template := x509.Certificate{
		SerialNumber: serial,
		Subject: pkix.Name{
			Organization:       []string{org},
			OrganizationalUnit: []string{ou},
			CommonName:         cn,
			Country:            []string{country},
			Locality:           []string{opt.Locality},
			Province:           []string{opt.Province},
		},
		NotBefore:             notBefore,
		NotAfter:              notAfter,
		KeyUsage:              x509.KeyUsageKeyEncipherment | x509.KeyUsageDigitalSignature,
		ExtKeyUsage:           []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth},
		BasicConstraintsValid: true,
		DNSNames:              dns,
		IPAddresses:           ips,
	}

	der, err := x509.CreateCertificate(rand.Reader, &template, &template, &priv.PublicKey, priv)
	if err != nil {
		return nil, fmt.Errorf("create certificate: %w", err)
	}

	for _, p := range []string{certPath, keyPath} {
		if d := filepath.Dir(p); d != "." && d != "" {
			if err := os.MkdirAll(d, 0755); err != nil {
				return nil, err
			}
		}
	}

	certOut, err := os.OpenFile(certPath, os.O_WRONLY|os.O_CREATE|os.O_TRUNC, 0644)
	if err != nil {
		return nil, fmt.Errorf("cert file: %w", err)
	}
	defer certOut.Close()
	if err := pem.Encode(certOut, &pem.Block{Type: "CERTIFICATE", Bytes: der}); err != nil {
		return nil, err
	}

	keyOut, err := os.OpenFile(keyPath, os.O_WRONLY|os.O_CREATE|os.O_TRUNC, 0600)
	if err != nil {
		return nil, fmt.Errorf("key file: %w", err)
	}
	defer keyOut.Close()
	privBytes := x509.MarshalPKCS1PrivateKey(priv)
	if err := pem.Encode(keyOut, &pem.Block{Type: "RSA PRIVATE KEY", Bytes: privBytes}); err != nil {
		return nil, err
	}

	info := &CertSummary{
		CommonName:         cn,
		Organization:       org,
		OrganizationalUnit: ou,
		Country:            country,
		Locality:           opt.Locality,
		Province:           opt.Province,
		DNSNames:           append([]string(nil), dns...),
		NotBefore:          notBefore,
		NotAfter:           notAfter,
		SerialNumberHex:    strings.TrimLeft(strings.ToUpper(serial.Text(16)), "0"),
		IsCA:               false,
	}
	if info.SerialNumberHex == "" {
		info.SerialNumberHex = "0"
	}
	for _, ip := range ips {
		if ip != nil {
			info.IPAddresses = append(info.IPAddresses, ip.String())
		}
	}
	return info, nil
}
