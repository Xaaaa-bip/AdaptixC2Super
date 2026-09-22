package main

import (
	"net"
	"net/http"
	"sync"
	"time"

	"github.com/gin-gonic/gin"
)

/* ===== 扫描器封禁 =====
 *
 * 公网监听器天天被扫（`/cgi-bin/sync`、`/wework/check`、`/autodiscover/...`）。以前每个
 * 不匹配的请求都会打一行 `beacon check-in rejected: ...`，实测一天能刷出 ~900MB 日志，
 * 把磁盘慢慢吃光 —— 而且真正有价值的那几行也被埋了。
 *
 * 这里按**源 IP** 记账：窗口内无效请求达到阈值就封禁一段时间。封禁期间**直接断开连接**、
 * 不回任何内容、也不再打日志；每个 IP 每个窗口只在**第一条**无效请求时打一行，跨过阈值时
 * 再打一行「已封禁」。阈值/窗口/时长可用监听器配置覆盖（见 TransportConfig 的
 * `scanner_ban_*` 三个键），都没给就用下面的默认值。
 *
 * ★ 为什么封禁键用 TCP 对端（RemoteAddr）而不是 X-Forwarded-For：那一列客户端可伪造，
 *   别人可以拿它把你的**真实 beacon 源 IP** 封掉，等于替对手做了拒绝服务。
 *   同理，开了可信前置（TrustXForwardedFor=true，典型是 CDN 回源）时对端是 CDN 的地址，
 *   封它会把链路整个掐死 —— 所以那种情况下**只降噪、不断连**（仍回 404 诱饵页）。 */

const (
	scannerBanThreshold = 8                // 窗口内无效请求数达到即封
	scannerBanWindow    = 60 * time.Second // 计数窗口
	scannerBanDuration  = 30 * time.Minute // 封禁时长
	scannerBanMaxHosts  = 4096             // 表上限：被海量源 IP 灌的时候优先保内存
)

type scannerEntry struct {
	fails       int
	windowStart time.Time
	bannedTill  time.Time
	logged      bool // 本窗口是否已打过日志（只打第一条，避免刷屏）
}

type scannerGuard struct {
	mu        sync.Mutex
	hosts     map[string]*scannerEntry
	threshold int
	window    time.Duration
	banFor    time.Duration
	enabled   bool
	lastPrune time.Time
}

func newScannerGuard(cfg TransportConfig) *scannerGuard {
	g := &scannerGuard{
		hosts:     make(map[string]*scannerEntry),
		threshold: scannerBanThreshold,
		window:    scannerBanWindow,
		banFor:    scannerBanDuration,
		enabled:   !cfg.ScannerBanDisabled,
	}
	if cfg.ScannerBanThreshold > 0 {
		g.threshold = cfg.ScannerBanThreshold
	}
	if cfg.ScannerBanWindowSec > 0 {
		g.window = time.Duration(cfg.ScannerBanWindowSec) * time.Second
	}
	if cfg.ScannerBanDurationSec > 0 {
		g.banFor = time.Duration(cfg.ScannerBanDurationSec) * time.Second
	}
	return g
}

// clientKey 取封禁用的源标识：**永远用 TCP 对端**，不用可伪造的头。
func clientKey(ctx *gin.Context) string {
	host, _, err := net.SplitHostPort(ctx.Request.RemoteAddr)
	if err != nil {
		return ctx.Request.RemoteAddr
	}
	return host
}

// banned 只读检查（顺带做懒清理，不额外起 goroutine）。
func (g *scannerGuard) banned(ip string) bool {
	if !g.enabled || ip == "" {
		return false
	}
	now := time.Now()
	g.mu.Lock()
	defer g.mu.Unlock()
	g.pruneLocked(now)

	e := g.hosts[ip]
	return e != nil && now.Before(e.bannedTill)
}

// fail 记一次无效请求。返回 (该源当前是否处于封禁, 这一条是否该打日志)。
func (g *scannerGuard) fail(ip string) (bool, bool) {
	if !g.enabled || ip == "" {
		return false, true
	}
	now := time.Now()
	g.mu.Lock()
	defer g.mu.Unlock()
	g.pruneLocked(now)

	e := g.hosts[ip]
	if e == nil {
		if len(g.hosts) >= scannerBanMaxHosts {
			return false, false // 表满：这条既不计也不打（降噪优先，别被灌爆内存）
		}
		e = &scannerEntry{windowStart: now}
		g.hosts[ip] = e
	}
	if now.Sub(e.windowStart) > g.window { // 进入新窗口，重新计数
		e.fails = 0
		e.windowStart = now
		e.logged = false
	}
	e.fails++

	if now.Before(e.bannedTill) {
		return true, false // 已在封禁中，静默丢弃
	}
	if e.fails >= g.threshold {
		e.bannedTill = now.Add(g.banFor)
		e.logged = true
		return true, true // 打一行「已封禁」
	}
	if e.logged {
		return false, false
	}
	e.logged = true
	return false, true // 本窗口第一条，打一行
}

// drop 处理已封禁来源的当前这条连接：默认**直接断开**，不回任何内容、不打日志。
// 开了可信前置（XFF 可伪造 / 对端是 CDN）时只回 404，避免把回源 IP 封死。
func (g *scannerGuard) drop(ctx *gin.Context, trustXForwardedFor bool) {
	if !trustXForwardedFor {
		if hj, ok := ctx.Writer.(http.Hijacker); ok {
			if conn, _, err := hj.Hijack(); err == nil {
				_ = conn.Close()
				ctx.Abort()
				return
			}
		}
	}
	ctx.AbortWithStatus(http.StatusNotFound)
}

func (g *scannerGuard) pruneLocked(now time.Time) {
	if now.Sub(g.lastPrune) < g.window {
		return
	}
	g.lastPrune = now
	for ip, e := range g.hosts {
		if now.After(e.bannedTill) && now.Sub(e.windowStart) > g.window {
			delete(g.hosts, ip)
		}
	}
}
