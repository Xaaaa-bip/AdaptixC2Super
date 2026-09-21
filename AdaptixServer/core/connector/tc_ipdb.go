package connector

import (
	"net/http"
	"os"
	"path/filepath"
	"sync"

	"github.com/gin-gonic/gin"

	"AdaptixServer/core/utils/ipdb"
	"AdaptixServer/core/utils/logs"
)

// IP → 归属地。
//
// 这个接口存在的理由：库（qqwry.ipdb，36MB）本来就在**服务器**上（dist/web/qqwry.ipdb），
// 而原设计是让**浏览器**把它整个拉下去在内存里查 —— 每次开控制台一次 36MB 下行，
// 而且取库路径写成绝对 `/qqwry.ipdb`，Web 一挂到子路径（profile 的 Web.path）就 404，
// 失败还只在 console.warn 留一行，界面上表现为「归属地」整列空白、看不出原因。
//
// 查询挪到服务端之后：前端一次批量问一屏主机的 IP，回包几十字节。
var (
	ipdbOnce   sync.Once
	ipdbDB     *ipdb.DB
	ipdbErr    error
	ipdbLoaded string
)

// resolveIpdbPath 按**可能性从高到低**找库文件。
//
//	tc.webUI.Root（profile 的 Web.root 解析出的绝对路径）/qqwry.ipdb   ← 现网实际位置
//	web/qqwry.ipdb                                                  ← cwd = dist 时的等价路径
//	qqwry.ipdb                                                      ← cwd 直接对着 web/
//
// 都找不到就返回空串 —— 调用方据此返回**可诊断的错误**，不是静默给空归属地。
func (tc *TsConnector) resolveIpdbPath() string {
	cands := make([]string, 0, 3)
	if tc.webUI != nil && tc.webUI.Root != "" {
		cands = append(cands, filepath.Join(tc.webUI.Root, "qqwry.ipdb"))
	}
	cands = append(cands, filepath.Join("web", "qqwry.ipdb"), "qqwry.ipdb")
	for _, p := range cands {
		if st, err := os.Stat(p); err == nil && !st.IsDir() {
			return p
		}
	}
	return ""
}

func (tc *TsConnector) ensureIpdb() (*ipdb.DB, error) {
	ipdbOnce.Do(func() {
		path := tc.resolveIpdbPath()
		if path == "" {
			ipdbErr = errIpdbMissing{}
			logs.Warn("", "IP geo: qqwry.ipdb not found (looked under Web.root / web/ / cwd) — 归属地会一直为空")
			return
		}
		db, err := ipdb.Open(path)
		if err != nil {
			ipdbErr = err
			logs.Error("", "IP geo: open %s: %v", path, err)
			return
		}
		ipdbDB, ipdbLoaded = db, path
		logs.Success("", "IP geo: loaded %s", path)
	})
	return ipdbDB, ipdbErr
}

// errIpdbMissing 带上"该怎么办"，别让操作员对着空归属地猜
type errIpdbMissing struct{}

func (errIpdbMissing) Error() string {
	return "qqwry.ipdb 不在服务端（找过 Web.root/qqwry.ipdb、web/qqwry.ipdb、qqwry.ipdb）—— 放到 dist/web/ 下即可"
}

type IpGeoReq struct {
	IPs []string `json:"ips"`
}

// TcIpGeo 批量查询：一次问一屏主机的 IP，避免每台一个请求。
//
// 请求 {"ips":["1.2.3.4", ...]}；响应 {"ok":true,"items":{"1.2.3.4":"北京 电信"}}
// 查不到的 IP 在 items 里是空串（前端照原样显示，不要当成错误）。
func (tc *TsConnector) TcIpGeo(ctx *gin.Context) {
	var req IpGeoReq
	if err := ctx.ShouldBindJSON(&req); err != nil {
		ctx.JSON(http.StatusOK, gin.H{"ok": false, "message": "invalid action"})
		return
	}
	db, err := tc.ensureIpdb()
	if err != nil {
		ctx.JSON(http.StatusOK, gin.H{"ok": false, "message": err.Error()})
		return
	}

	items := make(map[string]string, len(req.IPs))
	for _, ip := range req.IPs {
		if ip == "" {
			continue
		}
		if _, done := items[ip]; done {
			continue
		}
		items[ip] = db.Lookup(ip)
	}
	ctx.JSON(http.StatusOK, gin.H{"ok": true, "items": items})
}
