package profile

import (
	"os"
	"strings"
)

// ApplyEnvOverrides merges environment variables into the loaded profile (after YAML).
// Use this to avoid storing secrets in files checked into version control.
//
//   ADAPTIX_TEAMSERVER_PASSWORD           — overrides Teamserver.password
//   ADAPTIX_TEAMSERVER_PASSWORD_FILE      — read password from file (trimmed); wins over ADAPTIX_TEAMSERVER_PASSWORD
//   ADAPTIX_TEAMSERVER_CERT               — overrides Teamserver.cert path
//   ADAPTIX_TEAMSERVER_KEY                — overrides Teamserver.key path
//   ADAPTIX_TEAMSERVER_INTERFACE          — overrides Teamserver.interface
//   ADAPTIX_TEAMSERVER_MANAGE_PASSWORD    — overrides Teamserver.manage_password
//
// TLS autogen (see tls_autogen on Teamserver):
//   ADAPTIX_TLS_AUTOGEN=1                 — enable self-signed cert generation when profile tls_autogen is false
//   ADAPTIX_TLS_AUTOGEN_FORCE=1           — overwrite existing cert/key on startup
//   ADAPTIX_TLS_AUTOGEN_CN                — default certificate Common Name (SAN includes this + localhost)
func ApplyEnvOverrides(p *AdaptixProfile) {
	if p == nil || p.Server == nil {
		return
	}

	s := p.Server

	if path := strings.TrimSpace(os.Getenv("ADAPTIX_TEAMSERVER_PASSWORD_FILE")); path != "" {
		if b, err := os.ReadFile(path); err == nil {
			s.Password = strings.TrimSpace(string(b))
		}
	} else if v := strings.TrimSpace(os.Getenv("ADAPTIX_TEAMSERVER_PASSWORD")); v != "" {
		s.Password = v
	}

	if v := strings.TrimSpace(os.Getenv("ADAPTIX_TEAMSERVER_CERT")); v != "" {
		s.Cert = v
	}
	if v := strings.TrimSpace(os.Getenv("ADAPTIX_TEAMSERVER_KEY")); v != "" {
		s.Key = v
	}
	if v := strings.TrimSpace(os.Getenv("ADAPTIX_TEAMSERVER_INTERFACE")); v != "" {
		s.Interface = v
	}
	if v := strings.TrimSpace(os.Getenv("ADAPTIX_TEAMSERVER_MANAGE_PASSWORD")); v != "" {
		s.ManagePassword = v
	}
}
