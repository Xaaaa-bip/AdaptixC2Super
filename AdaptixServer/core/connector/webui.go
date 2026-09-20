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
	"regexp"
	"strings"

	"github.com/gin-gonic/gin"
)

// WebUI holds validated console static-serving settings (only when -web).
type WebUI struct {
	Interface string
	Port      int
	Path      string // normalized, e.g. "/" or "/cnmbzxc"
	Root      string // absolute static root (empty when using built-in placeholder)
	Spa       bool
	ApiBase   string
	Endpoint  string // Teamserver.endpoint, e.g. "/api"
	// Placeholder is true when dist/web is missing or incomplete; serve a diagnostic page instead of SPA.
	Placeholder bool
	// MissingReason explains why placeholder mode was chosen (for logs / page body).
	MissingReason string
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

// staticAssetExt returns true for paths that must be real files (never SPA-fallback to index.html).
// Serving index.html as application/javascript is the classic Vite white-screen bug.
func staticAssetExt(name string) bool {
	ext := strings.ToLower(filepath.Ext(name))
	switch ext {
	case ".js", ".mjs", ".cjs", ".css", ".map",
		".woff", ".woff2", ".ttf", ".otf", ".eot",
		".png", ".jpg", ".jpeg", ".gif", ".svg", ".webp", ".ico",
		".wasm", ".json", ".txt", ".xml", ".webmanifest",
		".mp3", ".mp4", ".webm", ".ipdb":
		return true
	default:
		return false
	}
}

// inspectWebRoot checks that root has index.html and the hashed assets it references (if any).
// Returns abs root and a non-empty reason when the tree is unusable for SPA.
func inspectWebRoot(root string) (abs string, reason string) {
	abs, err := filepath.Abs(root)
	if err != nil {
		return "", fmt.Sprintf("Web.root resolve failed: %v", err)
	}
	st, err := os.Stat(abs)
	if err != nil {
		if os.IsNotExist(err) {
			return abs, fmt.Sprintf("Web.root %q does not exist (copy AdaptixWeb build into dist/web, or run: make web)", root)
		}
		return abs, fmt.Sprintf("Web.root %q: %v", root, err)
	}
	if !st.IsDir() {
		return abs, fmt.Sprintf("Web.root %q is not a directory", root)
	}

	indexPath := filepath.Join(abs, "index.html")
	raw, err := os.ReadFile(indexPath)
	if err != nil {
		if os.IsNotExist(err) {
			return abs, fmt.Sprintf("Web.root %q missing index.html", root)
		}
		return abs, fmt.Sprintf("read index.html: %v", err)
	}
	if len(strings.TrimSpace(string(raw))) == 0 {
		return abs, "index.html is empty"
	}

	// Parse script/link asset refs from built index.html (Vite: /base/assets/index-XXXX.js).
	// Only .js/.css/.mjs matter for white-screen; try path as-is and strip first segment (Web.path).
	re := regexp.MustCompile(`(?i)(?:src|href)=["']([^"']+)["']`)
	var missing []string
	for _, m := range re.FindAllStringSubmatch(string(raw), -1) {
		ref := strings.TrimSpace(m[1])
		if ref == "" || strings.HasPrefix(ref, "data:") || strings.HasPrefix(ref, "http://") ||
			strings.HasPrefix(ref, "https://") || strings.HasPrefix(ref, "//") {
			continue
		}
		if i := strings.IndexAny(ref, "?#"); i >= 0 {
			ref = ref[:i]
		}
		cleanRef := path.Clean("/" + strings.TrimPrefix(ref, "/"))
		low := strings.ToLower(cleanRef)
		if !strings.HasSuffix(low, ".js") && !strings.HasSuffix(low, ".mjs") && !strings.HasSuffix(low, ".css") {
			continue
		}
		rel0 := strings.TrimPrefix(cleanRef, "/")
		candidates := []string{rel0}
		if parts := strings.Split(rel0, "/"); len(parts) >= 2 {
			candidates = append(candidates, strings.Join(parts[1:], "/"))
		}
		// Also try basename under assets/
		base := path.Base(cleanRef)
		if base != "" && base != "." {
			candidates = append(candidates, path.Join("assets", base))
		}
		found := false
		for _, cand := range candidates {
			if cand == "" || cand == "." {
				continue
			}
			fp := filepath.Join(abs, filepath.FromSlash(cand))
			if st, err := os.Stat(fp); err == nil && !st.IsDir() && st.Size() > 0 {
				found = true
				break
			}
		}
		if !found {
			missing = append(missing, ref)
		}
	}

	if len(missing) > 0 {
		return abs, fmt.Sprintf("index.html references missing assets: %s (rebuild/copy AdaptixWeb → dist/web)", strings.Join(missing, ", "))
	}

	// Soft check: empty/missing assets/ is almost always broken for Vite builds that use /assets/.
	if strings.Contains(string(raw), "assets/") {
		assetsDir := filepath.Join(abs, "assets")
		st, err := os.Stat(assetsDir)
		if err != nil || !st.IsDir() {
			return abs, "Web.root/assets missing (Vite bundle not deployed)"
		}
		entries, _ := os.ReadDir(assetsDir)
		hasFile := false
		for _, e := range entries {
			if !e.IsDir() {
				hasFile = true
				break
			}
		}
		if !hasFile {
			return abs, "Web.root/assets is empty (Vite bundle missing)"
		}
	}

	return abs, ""
}

// ValidateAndLoadWebUI prepares Web UI settings when enableWeb is true.
// Returns (nil, nil) when Web is not requested.
// When static files are missing/incomplete, returns a Placeholder WebUI (no hard fail) so
// -web still starts API and shows a diagnostic page instead of a blank SPA shell.
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

	absRoot, reason := inspectWebRoot(ui.Root)
	ui.Root = absRoot
	if reason != "" {
		ui.Placeholder = true
		ui.MissingReason = reason
		logs.Warn("", "Web UI incomplete — serving diagnostic page at %s: %s", ui.Path, reason)
		return ui, nil
	}
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

		// Incomplete / missing AdaptixWeb build: always show diagnostic page (not blank SPA).
		if ui.Placeholder {
			tc.serveWebPlaceholder(w, ui)
			return
		}

		rootFS := http.Dir(ui.Root)

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
				http.ServeFile(w, r, filepath.Join(ui.Root, filepath.FromSlash(name)))
				return
			}
		}

		// Missing .js/.css must 404 — never SPA-fallback (would white-screen the console).
		if staticAssetExt(name) {
			http.NotFound(w, r)
			return
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

func (tc *TsConnector) serveWebPlaceholder(w http.ResponseWriter, ui *WebUI) {
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.Header().Set("Cache-Control", "no-store")
	w.WriteHeader(http.StatusServiceUnavailable)
	reason := ui.MissingReason
	if reason == "" {
		reason = "Web static files are missing or incomplete"
	}
	root := ui.Root
	if root == "" {
		root = "web"
	}
	body := fmt.Sprintf(`<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8"/>
  <meta name="viewport" content="width=device-width, initial-scale=1"/>
  <title>Adaptix Web UI — 未部署</title>
  <style>
    :root { color-scheme: dark light; }
    body { font-family: ui-sans-serif, system-ui, sans-serif; margin: 0; min-height: 100vh;
      display: flex; align-items: center; justify-content: center;
      background: #0b0d10; color: #e8eaed; }
    .card { max-width: 40rem; margin: 1.5rem; padding: 1.75rem 1.5rem; border-radius: 12px;
      border: 1px solid #2a3140; background: #12161c; box-shadow: 0 12px 40px rgba(0,0,0,.35); }
    h1 { font-size: 1.15rem; margin: 0 0 .75rem; font-weight: 600; }
    p, li { line-height: 1.55; color: #b8c0cc; font-size: .95rem; }
    code, pre { font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; font-size: .85rem; }
    code { background: #1c222c; padding: .1rem .35rem; border-radius: 4px; color: #9ecbff; }
    pre { background: #1c222c; padding: .85rem 1rem; border-radius: 8px; overflow: auto; color: #d7dde7; }
    .reason { margin: 1rem 0; padding: .75rem 1rem; border-left: 3px solid #e6a23c;
      background: #1a1710; color: #f0d9a8; border-radius: 0 8px 8px 0; word-break: break-word; }
    ul { padding-left: 1.2rem; margin: .5rem 0 0; }
    a { color: #6cb6ff; }
  </style>
</head>
<body>
  <div class="card">
    <h1>Adaptix Web 控制台未部署</h1>
    <p>Teamserver 已用 <code>-web</code> 启动，但 <code>Web.root</code> 静态资源不可用，因此不会再回退成空白页。</p>
    <div class="reason">%s</div>
    <p>当前配置：</p>
    <ul>
      <li>Web.path = <code>%s</code></li>
      <li>Web.root = <code>%s</code></li>
      <li>API = <code>%s</code>（仍可用）</li>
    </ul>
    <p>修复（在 dist cwd 部署）：</p>
    <pre>npm run build -- --base=%s/
# 将 AdaptixWeb/dist/* 拷到 dist/web/
# 或: make web
# 然后重启: ./adaptixserver -profile profile.yaml -web</pre>
    <p>期望目录至少包含 <code>index.html</code> 与 <code>assets/*.js</code>、<code>assets/*.css</code>。</p>
  </div>
</body>
</html>`,
		htmlEscape(reason),
		htmlEscape(ui.Path),
		htmlEscape(root),
		htmlEscape(ui.Endpoint),
		htmlEscape(ui.Path),
	)
	_, _ = w.Write([]byte(body))
}

func htmlEscape(s string) string {
	replacer := strings.NewReplacer(
		"&", "&amp;",
		"<", "&lt;",
		">", "&gt;",
		`"`, "&quot;",
	)
	return replacer.Replace(s)
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
	mode := "static"
	if ui.Placeholder {
		mode = "placeholder (web files missing)"
	}
	if tc.samePortAsAPI(ui) {
		logs.Success("", "Web UI enabled -> https://%s:%d%s  (root=%s, api=%s, mode=%s)",
			ui.Interface, ui.Port, urlPath, ui.Root, ui.Endpoint, mode)
		return
	}
	if tc.webEngine == nil {
		logs.Error("", "Web UI engine not initialized")
		return
	}
	host := fmt.Sprintf("%s:%d", ui.Interface, ui.Port)
	go func() {
		logs.Success("", "Web UI enabled -> https://%s%s  (root=%s, separate port, api=%s, mode=%s)",
			host, urlPath, ui.Root, ui.Endpoint, mode)
		if err := tc.webEngine.RunTLS(host, tc.Cert, tc.Key); err != nil {
			logs.Error("", "Web UI listener failed: "+err.Error())
		}
	}()
}
