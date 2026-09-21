// Package ipdb 读 MaxMind 风格的 IPDB（纯真 qqwry.ipdb），只做 IPv4 的 IP → 归属地。
//
// 为什么搬到服务端（2026-09-21）：
//   原来是**浏览器本地查库** —— 控制台要把 qqwry.ipdb（36MB）整个拉下来在内存里二分。
//   两个问题：① 每次开控制台都是一次 36MB 的下行；② 取库的路径写成绝对的 `/qqwry.ipdb`，
//   一旦 Web 挂在子路径下（profile 的 Web.path）就打到站点根、拿到诱饵 404 页，
//   而失败只在 console.warn 留一行 —— 界面上表现为「归属地」整列空白，根本看不出原因。
//   库本来就在服务器上（dist/web/qqwry.ipdb），查询挪过来就只剩几十字节的往返。
//
// 文件格式（与前端 utils/ipdb.ts、Qt IpdbReader 同源）：
//
//	[0:4]            big-endian uint32 = meta 长度
//	[4:4+metaLen]    JSON: {node_count, total_size, fields[], languages{CN:off}, v4node?}
//	[4+metaLen:]     数据段，共 total_size 字节
//
// 数据段里前 node_count*8 字节是二叉树的节点（每节点两个 BE uint32 子指针），
// 越过 node_count 的指针是「命中的记录」而不是节点。
package ipdb

import (
	"encoding/binary"
	"encoding/json"
	"fmt"
	"os"
	"strings"
	"sync"
)

type meta struct {
	NodeCount int            `json:"node_count"`
	TotalSize int            `json:"total_size"`
	Fields    []string       `json:"fields"`
	Languages map[string]int `json:"languages"`
	V4Node    *int           `json:"v4node"`
}

// DB 一份已加载的库。加载后只读，可以并发查 —— 用 RWMutex 护住懒加载与缓存。
type DB struct {
	mu         sync.RWMutex
	data       []byte
	nodeCount  uint32
	v4Offset   uint32
	langOff    int
	fieldCount int
	cache      map[string]string
	path       string
}

const maxCache = 8192

// Open 加载一个 ipdb 文件。整份读进内存：查一次要跳 32 层节点，
// 每次读盘会让一屏主机的解析变成几百次 syscall。
func Open(path string) (*DB, error) {
	raw, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	if len(raw) < 8 {
		return nil, fmt.Errorf("ipdb too small: %d bytes", len(raw))
	}
	metaLen := int(binary.BigEndian.Uint32(raw[0:4]))
	if metaLen <= 0 || 4+metaLen > len(raw) {
		return nil, fmt.Errorf("bad meta length %d (file %d bytes)", metaLen, len(raw))
	}
	var m meta
	if err := json.Unmarshal(raw[4:4+metaLen], &m); err != nil {
		return nil, fmt.Errorf("meta json: %w", err)
	}
	if m.NodeCount <= 0 || m.TotalSize <= 0 || 4+metaLen+m.TotalSize != len(raw) {
		return nil, fmt.Errorf("size mismatch: node_count=%d total_size=%d file=%d meta=%d",
			m.NodeCount, m.TotalSize, len(raw), metaLen)
	}
	if len(m.Fields) <= 0 {
		return nil, fmt.Errorf("meta has no fields")
	}

	// 语言表里优先取 CN；没有就退回第一个 —— 与前端 ipdb.ts 同口径
	langOff := 0
	if v, ok := m.Languages["CN"]; ok {
		langOff = v
	} else {
		for _, v := range m.Languages {
			langOff = v
			break
		}
	}

	d := &DB{
		data:       raw[4+metaLen : 4+metaLen+m.TotalSize],
		nodeCount:  uint32(m.NodeCount),
		langOff:    langOff,
		fieldCount: len(m.Fields),
		cache:      make(map[string]string, 256),
		path:       path,
	}
	if m.V4Node != nil {
		d.v4Offset = uint32(*m.V4Node)
	} else {
		d.v4Offset = d.computeV4Entry()
	}
	return d, nil
}

// Path 返回库文件路径（日志用）。
func (d *DB) Path() string { return d.path }

func (d *DB) readNode(node uint32, index int) uint32 {
	off := int(node)*8 + index*4
	if off+4 > len(d.data) {
		return 0
	}
	return binary.BigEndian.Uint32(d.data[off : off+4])
}

// computeV4Entry 走到 IPv4 子树的入口：先沿 96 个 0 位（IPv6 前缀 ::ffff:0:0/96），
// 再取 1 —— 这是社区约定，meta 里给了 v4node 就不会走到这里。
func (d *DB) computeV4Entry() uint32 {
	var node uint32
	for i := 0; i < 96 && node < d.nodeCount; i++ {
		bit := 0
		if i >= 80 {
			bit = 1
		}
		node = d.readNode(node, bit)
	}
	return node
}

// resolvePayload 把「越过 node_count 的指针」还原成记录：偏移要反过来算一遍。
func (d *DB) resolvePayload(node uint32) []byte {
	resolved := int(node) - int(d.nodeCount) + int(d.nodeCount)*8
	if resolved < 0 || resolved+2 > len(d.data) {
		return nil
	}
	sz := int(binary.BigEndian.Uint16(d.data[resolved : resolved+2]))
	if sz < 0 || resolved+2+sz > len(d.data) {
		return nil
	}
	return d.data[resolved+2 : resolved+2+sz]
}

func parseIPv4(ip string) (uint32, bool) {
	ip = strings.TrimSpace(ip)
	// 只认裸点分十进制；带端口/带方括号的形态在上层已经裁过
	var nb [4]uint32
	part, dots := 0, 0
	var cur int
	seen := false
	for i := 0; i < len(ip); i++ {
		c := ip[i]
		if c == '.' {
			if !seen || part >= 3 {
				return 0, false
			}
			nb[part] = uint32(cur)
			part++
			cur, seen = 0, false
			dots++
			continue
		}
		if c < '0' || c > '9' {
			return 0, false
		}
		cur = cur*10 + int(c-'0')
		seen = true
		if cur > 255 {
			return 0, false
		}
	}
	if !seen || dots != 3 {
		return 0, false
	}
	nb[3] = uint32(cur)
	return nb[0]<<24 | nb[1]<<16 | nb[2]<<8 | nb[3], true
}

// Lookup 返回「国家 省 市 ISP」这类精简串；查不到返回空串（调用方照原样显示）。
// 带缓存：主机列表每次重渲染都会问同样那几十个 IP。
func (d *DB) Lookup(ip string) string {
	v, ok := parseIPv4(ip)
	if !ok {
		return ""
	}
	key := ip

	d.mu.RLock()
	if v, hit := d.cache[key]; hit {
		d.mu.RUnlock()
		return v
	}
	d.mu.RUnlock()

	node := d.v4Offset
	for i := 0; i < 32; i++ {
		if node > d.nodeCount {
			break
		}
		bit := int((v >> (31 - i)) & 1)
		node = d.readNode(node, bit)
	}
	out := ""
	if node > d.nodeCount {
		out = d.format(d.resolvePayload(node))
	}

	d.mu.Lock()
	if len(d.cache) < maxCache {
		d.cache[key] = out
	}
	d.mu.Unlock()
	return out
}

// format 把记录的 tab 分隔字段拼成显示串：取当前语言那一段，
// 去空、去重、去掉单独的「中国」（只留「北京 电信」这种有信息的），最多 4 段。
func (d *DB) format(raw []byte) string {
	if len(raw) == 0 {
		return ""
	}
	all := strings.Split(string(raw), "\t")
	if d.langOff+d.fieldCount > len(all) {
		return ""
	}
	parts := make([]string, 0, 4)
	for i := 0; i < d.fieldCount; i++ {
		t := strings.TrimSpace(all[d.langOff+i])
		if t != "" {
			parts = append(parts, t)
		}
	}
	uniq := make([]string, 0, 4)
	for _, p := range parts {
		if p == "中国" {
			continue
		}
		dup := false
		for _, u := range uniq {
			if u == p {
				dup = true
				break
			}
		}
		if !dup {
			uniq = append(uniq, p)
		}
		if len(uniq) == 4 {
			break
		}
	}
	return strings.Join(uniq, " ")
}
