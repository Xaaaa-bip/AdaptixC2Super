package profile

type AdaptixProfile struct {
	Server     *TsProfile    `yaml:"Teamserver"`
	HttpServer *TsHttpServer `yaml:"HttpServer"`
	// Web optional console UI (enabled only with CLI -web).
	Web *TsWebConfig `yaml:"Web"`
}

// TsWebConfig controls the browser console listen address and entry path.
// Only active when adaptixserver is started with -web.
type TsWebConfig struct {
	// Bind address for the Web console. Default 0.0.0.0.
	Interface string `yaml:"interface"`
	// Listen port. Default 8080. Same as Teamserver.port → mount on the API TLS listener.
	Port int `yaml:"port"`
	// Browser entry path for the console, e.g. "/" or "/cnmbzxc". Default "/".
	Path string `yaml:"path"`
	// Static files directory (vite build output), relative to process cwd. Default "web".
	Root string `yaml:"root"`
	// SPA fallback to index.html for unknown paths under Path. Default true when omitted.
	Spa *bool `yaml:"spa"`
	// Default API base for the login form. Empty = same-origin Teamserver.endpoint.
	ApiBase string `yaml:"api_base"`
}

// SpaEnabled returns whether SPA fallback is on (default true).
func (w *TsWebConfig) SpaEnabled() bool {
	if w == nil || w.Spa == nil {
		return true
	}
	return *w.Spa
}

type TsProfile struct {
	Interface      string            `yaml:"interface"`
	Port           int               `yaml:"port"`
	Endpoint       string            `yaml:"endpoint"`
	Password       string            `yaml:"password"`
	ManagePassword string            `yaml:"manage_password"`
	OnlyPassword   bool              `yaml:"only_password"`
	Operators      map[string]string `yaml:"operators"`
	Cert           string            `yaml:"cert"`
	Key            string            `yaml:"key"`
	// tls_autogen: before startup validation, write self-signed PEM if missing (or always if tls_autogen_force).
	// Env: ADAPTIX_TLS_AUTOGEN=1, ADAPTIX_TLS_AUTOGEN_FORCE=1, ADAPTIX_TLS_AUTOGEN_CN=hostname
	TLSAutogen                   bool     `yaml:"tls_autogen"`
	TLSAutogenForce              bool     `yaml:"tls_autogen_force"`
	TLSAutogenCommonName         string   `yaml:"tls_autogen_common_name"`
	TLSAutogenOrganization       string   `yaml:"tls_autogen_organization"`
	TLSAutogenOrganizationalUnit string   `yaml:"tls_autogen_organizational_unit"`
	TLSAutogenCountry            string   `yaml:"tls_autogen_country"`
	TLSAutogenLocality           string   `yaml:"tls_autogen_locality"`
	TLSAutogenProvince           string   `yaml:"tls_autogen_province"`
	TLSAutogenValidDays          int      `yaml:"tls_autogen_valid_days"`
	TLSAutogenDNSNames           []string `yaml:"tls_autogen_dns_names"`
	Extenders                    []string `yaml:"extenders"`
	AxScripts                    []string `yaml:"axscripts"`
	ATokenLive                   int      `yaml:"access_token_live_hours"`
	RTokenLive                   int      `yaml:"refresh_token_live_hours"`
}

type TsHttpServer struct {
	DefaultResponseHeaders map[string]string `yaml:"default_response_headers"`
	RateLimit              *TsRateLimit      `yaml:"rate_limit"`
	Error                  *TsHttpError      `yaml:"error"`
	HTTP                   *TsHTTPConfig     `yaml:"http"`
	TLS                    *TsTLSConfig      `yaml:"tls"`
}

// TsRateLimit optional brute-force mitigation for public auth routes.
// LoginRequestsPerMinute: max POST /login and /refresh per client IP per rolling minute; 0 = disabled.
type TsRateLimit struct {
	LoginRequestsPerMinute int `yaml:"login_requests_per_minute"`
}

type TsHttpError struct {
	Status      int               `yaml:"status"`
	Headers     map[string]string `yaml:"headers"`
	PagePath    string            `yaml:"page"`
	PageContent string
}

type TsHTTPConfig struct {
	MaxHeaderBytes        int      `yaml:"max_header_bytes"`
	ReadHeaderTimeoutSec  int      `yaml:"read_header_timeout_sec"`
	ReadTimeoutSec        int      `yaml:"read_timeout_sec"`
	WriteTimeoutSec       int      `yaml:"write_timeout_sec"`
	IdleTimeoutSec        int      `yaml:"idle_timeout_sec"`
	RequestTimeoutSec     int      `yaml:"request_timeout_sec"`
	RequestTimeoutMessage string   `yaml:"request_timeout_message"`
	DisableKeepAlives     bool     `yaml:"disable_keep_alives"`
	EnableHTTP2           *bool    `yaml:"enable_http2"`
	TrustedProxies        []string `yaml:"trusted_proxies"`
}

type TsTLSConfig struct {
	MinVersion               string   `yaml:"min_version"`
	MaxVersion               string   `yaml:"max_version"`
	PreferServerCipherSuites *bool    `yaml:"prefer_server_cipher_suites"`
	CipherSuites             []string `yaml:"cipher_suites"`
	// ClientAuth: none (default) | request (verify cert if client sends one) | require (must present valid cert)
	ClientAuth   string `yaml:"client_auth"`
	ClientCAFile string `yaml:"client_ca_file"`
}
