package connector

import (
	"AdaptixServer/core/profile"
	"AdaptixServer/core/utils/logs"
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"os"
	"path"
	"path/filepath"
	"strings"

	"github.com/gin-gonic/gin"
)

// WebUI holds validated console static-serving settings (only when -web).
type WebUI struct {
	Interface string
	Port      int
	Path      string // normalized, e.g. "/" or "/cnmbzxc"
	Root      string // absolute static root
	Spa       bool
	ApiBase   string
	Endpoint  string // Teamserver.endpoint, e.g. "/api"
}

func normalizeUIPath(p string) string {
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

// ValidateAndLoadWebUI prepares Web UI settings when enableWeb is true.
// Returns (nil, nil) when Web is not requested.
func ValidateAndLoadWebUI(enableWeb bool, web *profile.TsWebConfig, endpoint string) (*WebUI, error) {
	if !enableWeb {
		return nil, nil
	}
	if web == nil {
		web = &profile.TsWebConfig{}
	}
	ui := &WebUI{
		Interface: web.Interface,
		Port:      web.Port,
		Path:      normalizeUIPath(web.Path),
		Root:      strings.TrimSpace(web.Root),
		Spa:       web.SpaEnabled(),
		ApiBase:   strings.TrimSpace(web.ApiBase),
		Endpoint:  strings.TrimSpace(endpoint),
	}
	if ui.Interface == "" {
		ui.Interface = "0.0.0.0"
	}
	if ui.Port == 0 {
		ui.Port = 8080
	}
	if ui.Root == "" {
		ui.Root = "web"
	}
	if ui.Endpoint == "" {
		ui.Endpoint = "/api"
	}
	if !strings.HasPrefix(ui.Endpoint, "/") {
		ui.Endpoint = "/" + ui.Endpoint
	}
	ui.Endpoint = strings.TrimRight(ui.Endpoint, "/")
	if ui.Endpoint == "" {
		ui.Endpoint = "/api"
	}

	// path vs endpoint conflict
	ep := ui.Endpoint
	wp := ui.Path
	if wp == ep || (wp != "/" && ep != "/" && (strings.HasPrefix(wp, ep+"/") || strings.HasPrefix(ep, wp+"/"))) {
		return nil, fmt.Errorf("Web.path %q conflicts with Teamserver.endpoint %q", wp, ep)
	}

	absRoot, err := filepath.Abs(ui.Root)
	if err != nil {
		return nil, fmt.Errorf("Web.root: %w", err)
	}
	st, err := os.Stat(absRoot)
	if err != nil {
		if os.IsNotExist(err) {
			return nil, fmt.Errorf("Web.root %q does not exist (build AdaptixWeb into this directory)", ui.Root)
		}
		return nil, fmt.Errorf("Web.root: %w", err)
	}
	if !st.IsDir() {
		return nil, fmt.Errorf("Web.root %q is not a directory", ui.Root)
	}
	if _, err := os.Stat(filepath.Join(absRoot, "index.html")); err != nil {
		return nil, fmt.Errorf("Web.root %q missing index.html", ui.Root)
	}
	ui.Root = absRoot
	return ui, nil
}

func (tc *TsConnector) samePortAsAPI(ui *WebUI) bool {
	if ui == nil {
		return false
	}
	return ui.Port == tc.Port
}

// MountWebUI registers static + SPA routes for the Web console.
func (tc *TsConnector) MountWebUI(ui *WebUI) error {
	if ui == nil {
		return nil
	}
	tc.webUI = ui
	handler := tc.webHTTPHandler(ui)

	if !tc.samePortAsAPI(ui) {
		gin.SetMode(gin.ReleaseMode)
		eng := gin.New()
		eng.Use(gin.Recovery())
		if tc.httpServer != nil && len(tc.httpServer.DefaultResponseHeaders) > 0 {
			eng.Use(defaultResponseHeadersMiddleware(tc.httpServer.DefaultResponseHeaders))
		}
		if ui.Path == "/" {
			eng.NoRoute(gin.WrapF(handler))
		} else {
			eng.GET(ui.Path, gin.WrapF(handler))
			eng.HEAD(ui.Path, gin.WrapF(handler))
			eng.GET(ui.Path+"/*filepath", gin.WrapF(handler))
			eng.HEAD(ui.Path+"/*filepath", gin.WrapF(handler))
		}
		tc.webEngine = eng
		return nil
	}

	// Same TLS port as API: API route groups already registered and win for /api/*.
	if ui.Path == "/" {
		tc.Engine.NoRoute(tc.webNoRoute(ui))
	} else {
		tc.Engine.GET(ui.Path, gin.WrapF(handler))
		tc.Engine.HEAD(ui.Path, gin.WrapF(handler))
		tc.Engine.GET(ui.Path+"/*filepath", gin.WrapF(handler))
		tc.Engine.HEAD(ui.Path+"/*filepath", gin.WrapF(handler))
	}
	return nil
}

func (tc *TsConnector) webNoRoute(ui *WebUI) gin.HandlerFunc {
	httpCfg := profile.TsHTTPConfig{}
	httpErr := profile.TsHttpError{Status: 404}
	if tc.httpServer != nil {
		if tc.httpServer.HTTP != nil {
			httpCfg = *tc.httpServer.HTTP
		}
		if tc.httpServer.Error != nil {
			httpErr = *tc.httpServer.Error
		}
	}
	static := tc.webHTTPHandler(ui)
	return func(c *gin.Context) {
		reqPath := c.Request.URL.Path
		// Never serve SPA under API endpoint.
		if tc.Endpoint != "" && (reqPath == tc.Endpoint || strings.HasPrefix(reqPath, tc.Endpoint+"/")) {
			limitTimeoutMiddleware(httpCfg)(c)
			if c.IsAborted() {
				return
			}
			default404Middleware(httpErr)(c)
			_ = c.Error(errors.New("NoRoute"))
			return
		}
		static(c.Writer, c.Request)
	}
}

func (tc *TsConnector) webHTTPHandler(ui *WebUI) http.HandlerFunc {
	rootFS := http.Dir(ui.Root)
	return func(w http.ResponseWriter, r *http.Request) {
		reqPath := r.URL.Path

		// Map URL → file path relative to UI root.
		rel := reqPath
		if ui.Path != "/" {
			if reqPath == ui.Path || reqPath == ui.Path+"/" {
				rel = "/"
			} else if strings.HasPrefix(reqPath, ui.Path+"/") {
				rel = strings.TrimPrefix(reqPath, ui.Path)
				if rel == "" {
					rel = "/"
				}
			} else {
				http.NotFound(w, r)
				return
			}
		}

		clean := path.Clean(rel)
		if !strings.HasPrefix(clean, "/") {
			clean = "/" + clean
		}

		if clean == "/config.json" {
			tc.serveWebConfig(w, ui)
			return
		}

		// Try real file under root (strip leading / for Open).
		name := strings.TrimPrefix(clean, "/")
		if name == "" || name == "." {
			http.ServeFile(w, r, filepath.Join(ui.Root, "index.html"))
			return
		}

		f, err := rootFS.Open(name)
		if err == nil {
			defer f.Close()
			st, err := f.Stat()
			if err == nil && !st.IsDir() {
				// Serve with correct path for content-type / range.
				http.ServeFile(w, r, filepath.Join(ui.Root, filepath.FromSlash(name)))
				return
			}
		}

		if ui.Spa {
			http.ServeFile(w, r, filepath.Join(ui.Root, "index.html"))
			return
		}
		http.NotFound(w, r)
	}
}

func (tc *TsConnector) serveWebConfig(w http.ResponseWriter, ui *WebUI) {
	type cfg struct {
		ApiBase  string `json:"api_base"`
		Endpoint string `json:"endpoint"`
		Path     string `json:"path"`
	}
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.Header().Set("Cache-Control", "no-store")
	_ = json.NewEncoder(w).Encode(cfg{
		ApiBase:  ui.ApiBase,
		Endpoint: ui.Endpoint,
		Path:     ui.Path,
	})
}

// StartWebListener logs Web URL; starts a second HTTPS listener when ports differ.
func (tc *TsConnector) StartWebListener() {
	ui := tc.webUI
	if ui == nil {
		return
	}
	urlPath := ui.Path
	if urlPath == "" {
		urlPath = "/"
	}
	if tc.samePortAsAPI(ui) {
		logs.Success("", "Web UI enabled -> https://%s:%d%s  (root=%s, api=%s)",
			ui.Interface, ui.Port, urlPath, ui.Root, ui.Endpoint)
		return
	}
	if tc.webEngine == nil {
		logs.Error("", "Web UI engine not initialized")
		return
	}
	host := fmt.Sprintf("%s:%d", ui.Interface, ui.Port)
	go func() {
		logs.Success("", "Web UI enabled -> https://%s%s  (root=%s, separate port, api=%s)",
			host, urlPath, ui.Root, ui.Endpoint)
		if err := tc.webEngine.RunTLS(host, tc.Cert, tc.Key); err != nil {
			logs.Error("", "Web UI listener failed: "+err.Error())
		}
	}()
}
