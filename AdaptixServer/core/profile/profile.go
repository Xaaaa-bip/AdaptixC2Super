package profile

import (
	"AdaptixServer/core/utils/logs"
	isvalid "AdaptixServer/core/utils/valid"
	"errors"
	"os"
	"strings"
)

func tlsVersionRank(v string) (int, bool) {
	s := normalizeTLSVersion(v)
	switch s {
	case "", "DEFAULT":
		return 0, true
	case "TLS10", "TLS1.0":
		return 10, true
	case "TLS11", "TLS1.1":
		return 11, true
	case "TLS12", "TLS1.2":
		return 12, true
	case "TLS13", "TLS1.3":
		return 13, true
	default:
		return 0, false
	}
}

func normalizeTLSVersion(v string) string {
	s := strings.TrimSpace(strings.ToUpper(v))
	s = strings.ReplaceAll(s, "_", "")
	s = strings.ReplaceAll(s, "-", "")
	return s
}

func isAllowedTLSVersion(v string) bool {
	s := normalizeTLSVersion(v)
	switch s {
	case "", "DEFAULT", "TLS10", "TLS1.0", "TLS11", "TLS1.1", "TLS12", "TLS1.2", "TLS13", "TLS1.3":
		return true
	default:
		return false
	}
}

func (p *AdaptixProfile) applyDefaults() {
	if p.HttpServer == nil {
		p.HttpServer = &TsHttpServer{}
	}
	if p.HttpServer.Error == nil {
		p.HttpServer.Error = &TsHttpError{}
	}
	if p.HttpServer.Error.Status == 0 {
		p.HttpServer.Error.Status = 404
	}
	if p.HttpServer.Error.Headers == nil {
		p.HttpServer.Error.Headers = map[string]string{}
	}

	if p.HttpServer.HTTP == nil {
		p.HttpServer.HTTP = &TsHTTPConfig{}
	}
	if p.HttpServer.HTTP.MaxHeaderBytes == 0 {
		p.HttpServer.HTTP.MaxHeaderBytes = 8192
	}
	if p.HttpServer.HTTP.RequestTimeoutSec == 0 {
		p.HttpServer.HTTP.RequestTimeoutSec = 300
	}
	if p.HttpServer.HTTP.RequestTimeoutMessage == "" {
		p.HttpServer.HTTP.RequestTimeoutMessage = "504 Gateway Timeout"
	}

	if p.HttpServer.TLS == nil {
		p.HttpServer.TLS = &TsTLSConfig{}
	}
	if p.HttpServer.TLS.MinVersion == "" {
		p.HttpServer.TLS.MinVersion = "TLS1.2"
	}
	if p.HttpServer.TLS.MaxVersion == "" {
		p.HttpServer.TLS.MaxVersion = "TLS1.3"
	}

	if p.Web == nil {
		p.Web = &TsWebConfig{}
	}
	if p.Web.Interface == "" {
		p.Web.Interface = "0.0.0.0"
	}
	if p.Web.Port == 0 {
		p.Web.Port = 8080
	}
	if strings.TrimSpace(p.Web.Path) == "" {
		p.Web.Path = "/"
	} else {
		p.Web.Path = normalizeWebPath(p.Web.Path)
	}
	if strings.TrimSpace(p.Web.Root) == "" {
		p.Web.Root = "web"
	}
}

// normalizeWebPath forces a leading slash and strips a trailing slash (except root).
func normalizeWebPath(p string) string {
	p = strings.TrimSpace(p)
	if p == "" {
		return "/"
	}
	if !strings.HasPrefix(p, "/") {
		p = "/" + p
	}
	if len(p) > 1 {
		p = strings.TrimRight(p, "/")
	}
	return p
}

func (p *AdaptixProfile) prepareHttpServer() {
	p.applyDefaults()

	if p.HttpServer != nil && p.HttpServer.Error != nil && p.HttpServer.Error.PagePath != "" {
		fileContent, err := os.ReadFile(p.HttpServer.Error.PagePath)
		if err != nil {
			logs.Error("", "'HttpServer.error.page': failed to read file: %v", err)
			return
		}
		p.HttpServer.Error.PageContent = string(fileContent)
	}
}

func NewProfile() *AdaptixProfile {
	return new(AdaptixProfile)
}

func (p *AdaptixProfile) IsValid() error {
	valid := true

	p.prepareHttpServer()

	if p.Server == nil {
		logs.Error("", "'Teamserver' must be set")
		valid = false
	} else {
		if p.Server.Port < 1 || 65535 < p.Server.Port {
			logs.Error("", "'Teamserver.port' must be between 1 and 65535 (current is %v)", p.Server.Port)
			valid = false
		}

		if isvalid.ValidUriString(p.Server.Endpoint) == false {
			logs.Error("'Teamserver.endpoint' must be valid URI value (current is %v)", p.Server.Endpoint)
			valid = false
		}

		if p.Server.Password == "" {
			logs.Error("", "'Teamserver.password' must be set")
			valid = false
		}

		if p.Server.Cert == "" {
			logs.Error("", "'Teamserver.cert' must be set")
			valid = false
		} else {
			_, err := os.Stat(p.Server.Cert)
			if err != nil {
				if os.IsNotExist(err) {
					logs.Error("", "'Teamserver.cert': file does not exists")
					valid = false
				} else {
					logs.Error("", "'Teamserver.cert': failed to stat file: %v", err)
					valid = false
				}
			}
		}

		if p.Server.Key == "" {
			logs.Error("", "'Teamserver.key' must be set")
			valid = false
		} else {
			_, err := os.Stat(p.Server.Key)
			if err != nil {
				if os.IsNotExist(err) {
					logs.Error("", "'Teamserver.key': file does not exists")
					valid = false
				} else {
					logs.Error("", "'Teamserver.key': failed to stat file: %v", err)
					valid = false
				}
			}
		}

		if p.Server.Extenders != nil {
			for _, ext := range p.Server.Extenders {
				if ext != "" {
					_, err := os.Stat(ext)
					if err != nil {
						if os.IsNotExist(err) {
							logs.Error("", "Extender %s: file does not exists", ext)
							valid = false
						} else {
							logs.Error("", "Extender %s: failed to stat file: %v", ext, err)
							valid = false
						}
					}
				}
			}
		}

		if p.Server.ATokenLive < 1 {
			logs.Error("", "'Teamserver.access_token_live_hours' must be set")
			valid = false
		}

		if p.Server.RTokenLive < 1 {
			logs.Error("", "'Teamserver.refresh_token_live_hours' must be set")
			valid = false
		}
	}

	if p.HttpServer == nil {
		logs.Error("", "'HttpServer' must be set")
		valid = false
	} else {
		if p.HttpServer.Error.Status < 100 || p.HttpServer.Error.Status > 999 {
			logs.Error("", "'HttpServer.error.status' must be valid HTTP status (current is %v)", p.HttpServer.Error.Status)
			valid = false
		}
		if p.HttpServer.Error.PagePath != "" {
			_, err := os.Stat(p.HttpServer.Error.PagePath)
			if err != nil {
				if os.IsNotExist(err) {
					logs.Error("", "'HttpServer.error.page': file does not exists")
					valid = false
				} else {
					logs.Error("", "'HttpServer.error.page': failed to stat file: %v", err)
					valid = false
				}
			}
			if p.HttpServer.Error.PageContent == "" {
				logs.Error("", "'HttpServer.error.page': file is empty or cannot be read")
				valid = false
			}
		}
		if p.HttpServer.HTTP.MaxHeaderBytes < 0 {
			logs.Error("", "'HttpServer.http.max_header_bytes' must be >= 0")
			valid = false
		}
		if p.HttpServer.HTTP.ReadHeaderTimeoutSec < 0 {
			logs.Error("", "'HttpServer.http.read_header_timeout_sec' must be >= 0")
			valid = false
		}
		if p.HttpServer.HTTP.ReadTimeoutSec < 0 {
			logs.Error("", "'HttpServer.http.read_timeout_sec' must be >= 0")
			valid = false
		}
		if p.HttpServer.HTTP.WriteTimeoutSec < 0 {
			logs.Error("", "'HttpServer.http.write_timeout_sec' must be >= 0")
			valid = false
		}
		if p.HttpServer.HTTP.IdleTimeoutSec < 0 {
			logs.Error("", "'HttpServer.http.idle_timeout_sec' must be >= 0")
			valid = false
		}
		if p.HttpServer.HTTP.RequestTimeoutSec < 0 {
			logs.Error("", "'HttpServer.http.request_timeout_sec' must be >= 0")
			valid = false
		}

		if p.HttpServer.RateLimit != nil {
			if p.HttpServer.RateLimit.LoginRequestsPerMinute < 0 {
				logs.Error("", "'HttpServer.rate_limit.login_requests_per_minute' must be >= 0")
				valid = false
			}
			if p.HttpServer.RateLimit.LoginRequestsPerMinute > 100000 {
				logs.Error("", "'HttpServer.rate_limit.login_requests_per_minute' must be <= 100000")
				valid = false
			}
		}

		if p.HttpServer.TLS.MinVersion != "" && !isAllowedTLSVersion(p.HttpServer.TLS.MinVersion) {
			logs.Error("", "'HttpServer.tls.min_version' has unsupported value (current is %v)", p.HttpServer.TLS.MinVersion)
			valid = false
		}
		if p.HttpServer.TLS.MaxVersion != "" && !isAllowedTLSVersion(p.HttpServer.TLS.MaxVersion) {
			logs.Error("", "'HttpServer.tls.max_version' has unsupported value (current is %v)", p.HttpServer.TLS.MaxVersion)
			valid = false
		}

		minRank, okMin := tlsVersionRank(p.HttpServer.TLS.MinVersion)
		maxRank, okMax := tlsVersionRank(p.HttpServer.TLS.MaxVersion)
		if okMin && okMax && minRank != 0 && maxRank != 0 && minRank > maxRank {
			logs.Error("", "'HttpServer.tls.min_version' must be <= 'HttpServer.tls.max_version' (current is %v > %v)", p.HttpServer.TLS.MinVersion, p.HttpServer.TLS.MaxVersion)
			valid = false
		}

		clientAuth := strings.TrimSpace(strings.ToLower(p.HttpServer.TLS.ClientAuth))
		if clientAuth == "" {
			clientAuth = "none"
		}
		switch clientAuth {
		case "none":
		case "request", "require":
			if p.HttpServer.TLS.ClientCAFile == "" {
				logs.Error("", "'HttpServer.tls.client_ca_file' is required when client_auth is %q", clientAuth)
				valid = false
			} else {
				_, err := os.Stat(p.HttpServer.TLS.ClientCAFile)
				if err != nil {
					if os.IsNotExist(err) {
						logs.Error("", "'HttpServer.tls.client_ca_file': file does not exist")
						valid = false
					} else {
						logs.Error("", "'HttpServer.tls.client_ca_file': failed to stat file: %v", err)
						valid = false
					}
				}
			}
		default:
			logs.Error("", "'HttpServer.tls.client_auth' must be none, request, or require (current is %q)", p.HttpServer.TLS.ClientAuth)
			valid = false
		}
	}

	if p.Web != nil {
		if p.Web.Port < 1 || p.Web.Port > 65535 {
			logs.Error("", "'Web.port' must be between 1 and 65535 (current is %v)", p.Web.Port)
			valid = false
		}
		wp := normalizeWebPath(p.Web.Path)
		if wp == "" || !strings.HasPrefix(wp, "/") {
			logs.Error("", "'Web.path' must be an absolute URI path (current is %q)", p.Web.Path)
			valid = false
		}
		if p.Server != nil && p.Server.Endpoint != "" {
			ep := strings.TrimRight(strings.TrimSpace(p.Server.Endpoint), "/")
			if ep == "" {
				ep = "/"
			}
			// Reject path equal to API endpoint or nesting either way (ambiguous with SPA).
			conflict := wp == ep
			if !conflict && wp != "/" && ep != "/" {
				conflict = strings.HasPrefix(wp, ep+"/") || strings.HasPrefix(ep, wp+"/")
			}
			if conflict {
				logs.Error("", "'Web.path' %q conflicts with 'Teamserver.endpoint' %q", wp, p.Server.Endpoint)
				valid = false
			}
		}
	}

	if valid {
		return nil
	}
	return errors.New("Invalid profile\n")
}
