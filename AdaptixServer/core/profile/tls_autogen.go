package profile

import (
	"AdaptixServer/core/utils/logs"
	"AdaptixServer/core/utils/tlscert"
	"fmt"
	"net"
	"os"
	"path/filepath"
	"strings"
	"time"
)

// RunTLSAutogenIfConfigured writes a self-signed cert/key when tls_autogen is enabled (profile or env)
// and files are missing, or when force is set. Paths are resolved relative to the profile file directory.
func RunTLSAutogenIfConfigured(profileYAMLPath string, p *AdaptixProfile) error {
	if p == nil || p.Server == nil {
		return nil
	}
	s := p.Server

	autogen := s.TLSAutogen
	if v := strings.TrimSpace(os.Getenv("ADAPTIX_TLS_AUTOGEN")); strings.EqualFold(v, "1") || strings.EqualFold(v, "true") || strings.EqualFold(v, "yes") {
		autogen = true
	}
	if !autogen {
		return nil
	}

	force := s.TLSAutogenForce
	if v := strings.TrimSpace(os.Getenv("ADAPTIX_TLS_AUTOGEN_FORCE")); strings.EqualFold(v, "1") || strings.EqualFold(v, "true") {
		force = true
	}

	absProfile, err := filepath.Abs(profileYAMLPath)
	if err != nil {
		return err
	}
	baseDir := filepath.Dir(absProfile)

	certPath := resolveProfilePath(s.Cert, baseDir)
	keyPath := resolveProfilePath(s.Key, baseDir)

	if certPath == "" || keyPath == "" {
		return nil
	}

	if !force {
		if fileReadable(certPath) && fileReadable(keyPath) {
			return nil
		}
	}

	cn := strings.TrimSpace(s.TLSAutogenCommonName)
	if cn == "" {
		cn = strings.TrimSpace(os.Getenv("ADAPTIX_TLS_AUTOGEN_CN"))
	}

	opt := tlscert.DisguiseOptions{
		CommonName:         cn,
		Organization:       strings.TrimSpace(s.TLSAutogenOrganization),
		OrganizationalUnit: strings.TrimSpace(s.TLSAutogenOrganizationalUnit),
		Locality:           strings.TrimSpace(s.TLSAutogenLocality),
		Province:           strings.TrimSpace(s.TLSAutogenProvince),
		Country:            strings.TrimSpace(s.TLSAutogenCountry),
		ValidDays:          s.TLSAutogenValidDays,
		DNSNames:           append([]string(nil), s.TLSAutogenDNSNames...),
	}

	if ip := net.ParseIP(strings.TrimSpace(s.Interface)); ip != nil && !ip.IsUnspecified() {
		opt.IPAddresses = append(opt.IPAddresses, ip)
	}

	needWrite := force || !fileReadable(certPath) || !fileReadable(keyPath)
	if needWrite {
		info, err := tlscert.WriteSelfSignedRSA(certPath, keyPath, opt)
		if err != nil {
			return err
		}
		logs.Success("", "TLS autogen: wrote self-signed PEM (cert=%s key=%s)", certPath, keyPath)
		logTLSAutogenCertDetails(info)
		return nil
	}

	info, err := tlscert.ReadCertSummary(certPath)
	if err != nil {
		return fmt.Errorf("tls_autogen: read existing cert %s: %w", certPath, err)
	}
	logs.Info("", "TLS autogen: existing certificate kept (use tls_autogen_force to regenerate)")
	logTLSAutogenCertDetails(info)
	return nil
}

func logTLSAutogenCertDetails(info *tlscert.CertSummary) {
	if info == nil {
		return
	}
	logs.Info("", "TLS autogen — subject: CN=%q  O=%q  OU=%q  C=%q  L=%q  ST=%q",
		info.CommonName, info.Organization, info.OrganizationalUnit, info.Country, info.Locality, info.Province)
	if len(info.DNSNames) > 0 {
		logs.Info("", "TLS autogen — SAN DNS: %s", strings.Join(info.DNSNames, ", "))
	}
	if len(info.IPAddresses) > 0 {
		logs.Info("", "TLS autogen — SAN IP: %s", strings.Join(info.IPAddresses, ", "))
	}
	logs.Info("", "TLS autogen — serial (hex): %s", info.SerialNumberHex)
	logs.Info("", "TLS autogen — validity: %s  →  %s  (%s)",
		info.NotBefore.UTC().Format(time.RFC3339),
		info.NotAfter.UTC().Format(time.RFC3339),
		info.NotAfter.Sub(info.NotBefore).Round(time.Hour).String())
	if info.IsCA {
		logs.Info("", "TLS autogen — CA: true")
	}
}

func resolveProfilePath(p, baseDir string) string {
	p = strings.TrimSpace(p)
	if p == "" {
		return ""
	}
	if filepath.IsAbs(p) {
		return filepath.Clean(p)
	}
	return filepath.Clean(filepath.Join(baseDir, p))
}

func fileReadable(path string) bool {
	st, err := os.Stat(path)
	return err == nil && !st.IsDir() && st.Size() > 0
}
