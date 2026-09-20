package main

import (
	"encoding/json"
	"fmt"
	"sync"

	adaptix "github.com/Adaptix-Framework/axc2"
	"gopkg.in/yaml.v3"
)

/*
 * topology —— 拓扑图的服务端持久化。
 *
 * 为什么是插件而不是改核心：teamserver 已经给了插件三样东西，
 * 正好够用，一行核心 Go 都不用动：
 *   ① TsEndpointRegister(...)      —— 在**同一个 gin Engine、同一个端口**上挂
 *      JWT 保护的 /api 路由（handler 直接拿到操作员 username）；
 *   ② TsExtenderDataSave/Load(...) —— 落到**已存在**的 ExtenderData 表
 *      （core/database/database.go 里早有这张表，UNIQUE(ExtenderName, Key)），
 *      重启不丢；
 *   ③ TsServiceSendDataAll(...)    —— 主动推 WebSocket 给所有操作员。
 * 同样的路子 vnc / webdelivery / hosting_service / notifier 都在走。
 * 改核心则要重编 adaptixserver 并对账四棵树 —— 那正是本项目出过事故的地方。
 *
 * ★ 存储格式刻意做成「信封 + 不透明 blob」：
 *
 *	{ "rev": 7, "board": { ...前端爱放什么放什么... } }
 *
 * 服务端**只**理解 rev，图的形状（设备、连线、坐标、角色）完全归前端所有。
 * 于是前端以后加字段既不用改这个插件，也不用重编 .so 再重启一次。
 * 反过来若让服务端解析 devices/edges，每加一种设备类型都要动服务端。
 */

const (
	extenderName = "topology" // ExtenderData 的命名空间
	dataKey      = "graph"    // 全局共享一张图，所以只有一个 key
)

type PluginService struct{}

var (
	Ts        Teamserver
	ModuleDir string
	Config    TopologyConfig

	// graphLock 串行化「读 → 比 rev → 写」。
	// 乐观锁本身不能防并发：两个 POST 同时进来会双双读到 rev=5、双双通过校验、
	// 后写的那个静默覆盖先写的。必须把整段读改写放进同一把锁里。
	graphLock sync.Mutex
)

// Teamserver 是本插件真正用到的那一小撮方法。
// 插件自己声明窄接口再对 *server.Teamserver 做类型断言 —— 这是本项目插件
// 的通行写法（见 beacon_listener_http/pl_main.go），好处是核心新增方法
// 不会把这个插件编译坏。方法签名必须与 core/server 上的实现逐字一致。
type Teamserver interface {
	TsEndpointRegister(method string, path string, handler func(username string, body []byte) (int, []byte)) error
	TsExtenderDataSave(extenderName string, key string, value []byte) error
	TsExtenderDataLoad(extenderName string, key string) ([]byte, error)
	TsServiceSendDataAll(service string, data string)
}

type TopologyConfig struct {
	ApiRoute string `yaml:"api_route" json:"api_route"`
}

func InitPlugin(ts any, moduleDir string, serviceConfig string) adaptix.PluginService {
	Ts = ts.(Teamserver)
	ModuleDir = moduleDir

	Config = TopologyConfig{ApiRoute: "/topology"}
	if serviceConfig != "" {
		if err := yaml.Unmarshal([]byte(serviceConfig), &Config); err != nil {
			fmt.Printf("[TOPOLOGY] Error parsing config: %v\n", err)
		}
	}

	// 注册失败必须出声。静默失败的表现是「前端 404，但服务端一切正常」，
	// 而 404 会被误当成前端拼错了 URL，很难查回这里。
	if err := Ts.TsEndpointRegister("GET", Config.ApiRoute+"/graph", handleGraphGet); err != nil {
		fmt.Printf("[TOPOLOGY] Failed to register GET %s/graph: %v\n", Config.ApiRoute, err)
	}
	if err := Ts.TsEndpointRegister("POST", Config.ApiRoute+"/graph", handleGraphSave); err != nil {
		fmt.Printf("[TOPOLOGY] Failed to register POST %s/graph: %v\n", Config.ApiRoute, err)
	}

	fmt.Printf("[TOPOLOGY] Service initialized. API: %s\n", Config.ApiRoute)
	return &PluginService{}
}

func (p *PluginService) Call(operator string, function string, args string) {}

/// 信封

type graphDoc struct {
	Rev   int64           `json:"rev"`
	Board json.RawMessage `json:"board"`
}

func emptyDoc() *graphDoc {
	return &graphDoc{Rev: 0, Board: json.RawMessage(`{"v":1}`)}
}

func loadDoc() (*graphDoc, error) {
	raw, err := Ts.TsExtenderDataLoad(extenderName, dataKey)
	if err != nil {
		return nil, err
	}
	// ★ 没画过图时 DbExtenderDataLoad 返回 (nil, nil) 而不是 ErrNoRows
	// （见 core/database/db_extender.go），所以这里判 len 而不是判 err。
	// 若写成 if err != nil 就返回错误，首次打开拓扑页会 500。
	if len(raw) == 0 {
		return emptyDoc(), nil
	}

	var d graphDoc
	if err := json.Unmarshal(raw, &d); err != nil {
		// 存进去的东西坏了。**不要** 500 —— 那会让整个拓扑页永远打不开，
		// 而操作员并没有做错任何事。降级成空图 + 打日志，让页面至少能用。
		fmt.Printf("[TOPOLOGY] stored graph is corrupt, falling back to empty: %v\n", err)
		return emptyDoc(), nil
	}
	if len(d.Board) == 0 {
		d.Board = json.RawMessage(`{"v":1}`)
	}
	return &d, nil
}

/// 路由

func handleGraphGet(username string, body []byte) (int, []byte) {
	graphLock.Lock()
	defer graphLock.Unlock()

	doc, err := loadDoc()
	if err != nil {
		return 500, jsonErr("load failed: " + err.Error())
	}

	// 直接回信封本身，GET 与 WS 推送是**同一种形状**，
	// 前端可以只写一个 applyBoard() 供两条路径共用。
	out, err := json.Marshal(doc)
	if err != nil {
		return 500, jsonErr("marshal failed: " + err.Error())
	}
	return 200, out
}

func handleGraphSave(username string, body []byte) (int, []byte) {
	var req struct {
		BaseRev int64           `json:"base_rev"`
		Board   json.RawMessage `json:"board"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		return 400, jsonErr("invalid request: " + err.Error())
	}
	if len(req.Board) == 0 {
		return 400, jsonErr("board is required")
	}

	graphLock.Lock()
	defer graphLock.Unlock()

	cur, err := loadDoc()
	if err != nil {
		return 500, jsonErr("load failed: " + err.Error())
	}

	if req.BaseRev != cur.Rev {
		// 别人先改了。把当前版本一并回给前端，让它能直接重载，
		// 而不是只丢一句「冲突了」让它自己去猜现在是什么样。
		// 这也是验收里的阳性对照：过期 base_rev 必须被拒，不能静默覆盖。
		out, _ := json.Marshal(map[string]interface{}{
			"ok":       false,
			"conflict": true,
			"message":  "图已被他人修改",
			"rev":      cur.Rev,
			"board":    cur.Board,
		})
		return 409, out
	}

	next := graphDoc{Rev: cur.Rev + 1, Board: req.Board}
	raw, err := json.Marshal(next)
	if err != nil {
		return 500, jsonErr("marshal failed: " + err.Error())
	}
	if err := Ts.TsExtenderDataSave(extenderName, dataKey, raw); err != nil {
		return 500, jsonErr("save failed: " + err.Error())
	}

	// 广播给所有操作员，让他们实时看到这张共享的图。
	// ★ SyncPackerServiceData.Data 是 **string**（core/server/utils.go），
	// 所以前端拿到的是「一个装着 JSON 的字符串」，要再 parse 一次。
	Ts.TsServiceSendDataAll(extenderName, string(raw))

	out, _ := json.Marshal(map[string]interface{}{"ok": true, "rev": next.Rev})
	return 200, out
}

func jsonErr(msg string) []byte {
	data, _ := json.Marshal(map[string]interface{}{"ok": false, "message": msg})
	return data
}
