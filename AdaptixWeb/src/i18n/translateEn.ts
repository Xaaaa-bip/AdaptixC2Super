import { EN_DICT } from './dict.en'

export const ZH_RE = /[\u4e00-\u9fff]/

const CACHE = new Map<string, string>()
const MAX_CACHE = 4000

function setCache(key: string, val: string): string {
  if (CACHE.size >= MAX_CACHE) {
    // Evict oldest chunk simply
    let n = 0
    for (const k of CACHE.keys()) {
      CACHE.delete(k)
      if (++n >= 500) break
    }
  }
  CACHE.set(key, val)
  return val
}

function normWS(s: string): string {
  return s.replace(/\s+/g, ' ').trim()
}

function normPunct(s: string): string {
  return normWS(s)
    .replace(/（/g, '(')
    .replace(/）/g, ')')
    .replace(/，/g, ',')
    .replace(/、/g, ',')
    .replace(/：/g, ':')
    .replace(/；/g, ';')
    .replace(/。/g, '.')
    .replace(/「|『/g, '"')
    .replace(/」|』/g, '"')
    .replace(/\(\s+/g, '(')
    .replace(/\s+\)/g, ')')
    .replace(/\s*,\s*/g, ', ')
}

let NORM_DICT: Map<string, string> | null = null
function getNormDict(): Map<string, string> {
  if (NORM_DICT) return NORM_DICT
  NORM_DICT = new Map()
  for (const [k, v] of Object.entries(EN_DICT)) {
    const np = normPunct(k)
    if (!NORM_DICT.has(np)) NORM_DICT.set(np, v)
  }
  return NORM_DICT
}

function findInDict(s: string): string | undefined {
  if (EN_DICT[s] !== undefined) return EN_DICT[s]
  const ws = normWS(s)
  if (EN_DICT[ws] !== undefined) return EN_DICT[ws]
  const np = normPunct(s)
  return getNormDict().get(np)
}

function txRoleOrLabel(raw: string): string {
  const s = normWS(raw)
  return EN_DICT[s] ?? translateInner(s, 1)
}

type Rule = [RegExp, (...m: string[]) => string]

const DYNAMIC_RULES: Rule[] = [
  // Shell tabs & tiers
  [/^终端 · (.+)$/, (_, h) => `Console · ${h}`],
  [/^文件 · (.+)$/, (_, h) => `Files · ${h}`],
  [/^进程 · (.+)$/, (_, h) => `Processes · ${h}`],
  [/^第\s*(\d+)\s*层$/, (_, d) => `Tier ${d}`],

  // Relative timestamps & units
  [/^(\d+)\s*s 前$/i, (_, n) => `${n}s ago`],
  [/^(\d+)\s*m 前$/i, (_, n) => `${n}m ago`],
  [/^(\d+)\s*h 前$/i, (_, n) => `${n}h ago`],
  [/^(\d+)\s*d 前$/i, (_, n) => `${n}d ago`],
  [/^(\d+)\s*字节$/, (_, n) => `${n} bytes`],
  [/^·\s*(\d+)\s*个参数$/, (_, n) => `· ${n} args`],
  [/^(\d+)\s*参$/, (_, n) => `${n} args`],
  [/^(\d+)\s*个命令$/, (_, n) => `${n} commands`],
  [/^共\s*(\d+)\s*条$/, (_, n) => `${n} total`],
  [/^(\d+)\s*条$/, (_, n) => `${n} items`],
  [/^(\d+)\s*项$/, (_, n) => `${n} items`],
  [/^(\d+)\s*个$/, (_, n) => `${n}`],
  [/^(\d+)\s*个文件$/, (_, n) => `${n} files`],
  [/^(\d+)\s*个目标$/, (_, n) => `${n} targets`],
  [/^(\d+)\s*张截图$/, (_, n) => `${n} screenshots`],
  [/^(\d+)\s*条隧道$/, (_, n) => `${n} tunnels`],
  [/^(\d+)\s*个?(磁盘|挂载点)$/, (_, n, k) => `${n} ${k === '磁盘' ? 'disks' : 'mounts'}`],
  [/^(\d+)\s*执行中$/, (_, n) => `${n} running`],
  [/^延迟\s*(\d+)\s*ms$/, (_, n) => `Latency ${n} ms`],
  [/^3\. 结果 · (\d+) 个变体$/, (_, n) => `3. Results · ${n} variants`],
  [/^变体数量（≤\s*(\d+)）$/, (_, n) => `Variant Count (≤ ${n})`],

  // Build / Generate / Schemas
  [/^服务端参数表 \((\d+)\)$/, (_, n) => `Server Schema (${n})`],
  [/^内置参数表 \((\d+)\)$/, (_, n) => `Built-in Schema (${n})`],
  [/^已开启 (\d+) 项$/, (_, n) => `${n} enabled`],
  [/^参数键:\s*(.+)$/, (_, k) => `Key: ${k}`],
  [/^文件 (.+?) 超过上限 (.+)$/, (_, f, max) => `File ${f} exceeds limit (${max})`],
  [/^生成参数表拉取失败（(.+?)）：([\s\S]+)$/, (_, t, msg) => `Failed to fetch build schema (${t}): ${txRoleOrLabel(msg)}`],
  [/^生成 (.+?) \/ (.+?) \/ (.+)$/, (_, a, b, c) => `Build ${a} / ${b} / ${c}`],
  [/^使用 (.+?) 生成 (.+?) (.+)$/, (_, l, a, f) => `Build ${a} ${f} with ${l}`],
  [/^载荷已生成 (.+)$/, (_, fn) => `Payload generated: ${fn}`],
  [/^大小 (.+?) · 已触发浏览器下载$/, (_, sz) => `Size ${sz} · Browser download started`],
  [/^已下载 (.+)$/, (_, fn) => `Downloaded ${fn}`],
  [/^已重新下载 (.+)$/, (_, fn) => `Re-downloaded ${fn}`],
  [/^重连 (.+)$/, (_, r) => `Reconnect ${r}`],

  // WebDelivery & ArgFuscator
  [/^生成 (\d+) 个变体$/, (_, n) => `Generate ${n} Variant${n === '1' ? '' : 's'}`],
  [/^展开变体 \((\d+)\)$/, (_, n) => `Show Variants (${n})`],
  [/^· (\d+) 个已标注风险$/, (_, n) => `· ${n} flagged`],
  [/^已同步 · 缓存 (\d+) 条$/, (_, n) => `Synced · ${n} cached`],
  [/^WebDelivery 已同步 (.+)$/, (_, rest) => `WebDelivery synced: ${rest}`],
  [/^协议被改成 (.+)$/, (_, s) => `Protocol rewritten to ${s}`],
  [/^主机被改成 (.+?)（原 (.+?)）$/, (_, a, b) => `Host rewritten to ${a} (was ${b})`],
  [/^端口被改成 (.+?)（原 (.+?)）$/, (_, a, b) => `Port rewritten to ${a} (was ${b})`],
  [/^路径被改成 (.+?)（原 (.+?)）$/, (_, a, b) => `Path rewritten to ${a} (was ${b})`],
  [/^服务端没有可用的 pwsh：([\s\S]+)$/, (_, r) => `Server pwsh unavailable: ${txRoleOrLabel(r)}`],
  [/^参数混淆插件状态拉取失败：([\s\S]+)$/, (_, msg) => `ArgFuscator status fetch failed: ${txRoleOrLabel(msg)}`],
  [/^状态拉取失败:\s*([\s\S]+)$/, (_, msg) => `Status fetch failed: ${txRoleOrLabel(msg)}`],
  [/^参数混淆失败:\s*([\s\S]+)$/, (_, msg) => `ArgFuscator failed: ${txRoleOrLabel(msg)}`],
  [/^应用失败:\s*([\s\S]+)$/, (_, msg) => `Apply failed: ${txRoleOrLabel(msg)}`],
  [/^(.+?)：服务端没有返回任何变体$/, (_, s) => `${s}: server returned no variants`],
  [/^(.+?)：(\d+) 个变体里有 (\d+) 个已标注风险$/, (_, s, a, b) => `${s}: ${b}/${a} variants flagged`],
  [/^参数混淆 (.+?)：(\d+) 个变体，逐条校验通过$/, (_, s, a) => `ArgFuscator ${s}: ${a} variants verified`],
  [/^参数混淆 (.+?):\s*([\s\S]+)$/, (_, s, err) => `ArgFuscator ${s}: ${txRoleOrLabel(err)}`],
  [/^(.+?) 不在服务端的 (\d+) 个参数模型里，混淆一定失败$/, (_, tok, n) => `${tok} is not in server's ${n} arg models — obfuscation will fail`],
  [/^(.+?) 不在服务端的 (\d+) 个参数模型里，生成大概率失败。$/, (_, tok, n) => `${tok} is not in server's ${n} arg models — generation may fail.`],
  [/^监听 (.+?) 不在监听器列表里，请重新选择$/, (_, l) => `Listener ${l} not found — select another`],
  [/^监听 (.+?) \((.+?)\) 与 agent (.+?) 不兼容$/, (_, l, reg, a) => `Listener ${l} (${reg}) is incompatible with agent ${a}`],
  [/^已复制变体 (\d+)$/, (_, n) => `Copied variant #${n}`],
  [/^已复制 (\d+) 个变体$/, (_, n) => `Copied all ${n} variants`],
  [/^已复制 (.+)$/, (_, w) => `Copied ${txRoleOrLabel(w)}`],
  [/^已复制(.+)$/, (_, w) => `Copied ${txRoleOrLabel(w)}`],
  [/^(.+?)已复制$/, (_, w) => `${txRoleOrLabel(w)} copied`],

  // Listeners & Gopher
  [/^追加 (.+)$/, (_, a) => `Added ${a}`],
  [/^已套用「(.+?)」流量模板（URI \/ UA \/ 心跳头 \/ 诱饵页已同步）$/, (_, l) => `Applied "${txRoleOrLabel(l)}" profile (URI / UA / headers / decoy synced)`],
  [/^读取 (.+?) 失败：([\s\S]+)$/, (_, f, err) => `Failed to read ${f}: ${txRoleOrLabel(err)}`],
  [/^读取失败：([\s\S]+)$/, (_, err) => `Read failed: ${txRoleOrLabel(err)}`],
  [/^监听器 (.+?) \((.+?)\) 已创建$/, (_, n, t) => `Listener ${n} (${t}) created`],
  [/^监听器 (.+?) 已(创建|更新)$/, (_, n, op) => `Listener ${n} ${op === '创建' ? 'created' : 'updated'}`],
  [/^已(暂停|恢复|停止) (.+)$/, (_, op, n) => `${op === '暂停' ? 'Paused' : op === '恢复' ? 'Resumed' : 'Stopped'} ${n}`],
  [/^停止并删除 (.+?)？$/, (_, n) => `Stop and remove ${n}?`],
  [/^就绪 · 将以 (.+?)（(.+?)）启动监听$/, (_, t, n) => `Ready · Launching ${t} (${n})`],
  [/^libp2p 的回连地址必须是 multiaddr（以 \/ 开头）：([\s\S]+)$/, (_, a) => `libp2p callbacks must be multiaddrs starting with /: ${a}`],
  [/^l2p_relays 必须是 multiaddr（以 \/ 开头）：([\s\S]+)$/, (_, r) => `l2p_relays must be multiaddrs starting with /: ${r}`],
  [/^l2p_relays 里不应含 \/p2p-circuit（那段由 listener 自己补）：([\s\S]+)$/, (_, r) => `Omit /p2p-circuit from l2p_relays (appended automatically): ${r}`],
  [/^l2p_relays 缺 \/p2p\/<relayPeerID>：([\s\S]+)$/, (_, r) => `l2p_relays requires /p2p/<relayPeerID>: ${r}`],
  [/^l2p_addrs 必须是 multiaddr（以 \/ 开头）：([\s\S]+)$/, (_, a) => `l2p_addrs must be multiaddrs starting with /: ${a}`],
  [/^当前传输协议是 tcp，而回连地址是 multiaddr：([\s\S]+)$/, (_, a) => `Transport is tcp, but callback is a multiaddr (switch transport to libp2p): ${a}`],

  // Graph & Topology
  [/^标签:\s*(.+)$/, (_, tag) => `Tag: ${txRoleOrLabel(tag)}`],
  [/^拓扑图，共 (\d+) 台主机、(\d+) 个手工设备$/, (_, a, d) => `Topology: ${a} agents, ${d} manual devices`],
  [/^恢复自动排列（(\d+) 台）$/, (_, n) => `Reset Auto Layout (${n} hosts)`],
  [/^已添加(.+)$/, (_, l) => `Added ${txRoleOrLabel(l)}`],
  [/^已设为 (.+)$/, (_, r) => `Set role to ${txRoleOrLabel(r)}`],
  [/^已选起点「(.+?)」，点击目标节点\/设备即可连线$/, (_, l) => `Origin "${l}" selected — click destination node/device to connect`],
  [/^清空全部手工内容？\n+· (\d+) 个手工设备\n· (\d+) 条手工连线\n· (\d+) 个手工摆放的主机位置\n+（agent 主机本身不受影响）$/, (_, d, e, p) =>
    `Clear all manual canvas annotations?\n\n· ${d} devices\n· ${e} manual links\n· ${p} pinned host positions\n\n(Agent sessions remain unaffected)`],
  [/^(.+?) · (已控|未控)$/, (_, l, st) => `${txRoleOrLabel(l)} · ${st === '已控' ? 'Controlled' : 'Uncontrolled'}`],

  // AI Collab
  [/^连接成功（延迟 (\d+) ms），可以开始协同分析。$/, (_, ms) => `Connected (${ms} ms latency). Ready to collaborate.`],
  [/^汇总 (\d+)\/(\d+) 在线主机与重点推进目标$/, (_, a, b) => `Summarize ${a}/${b} online hosts & priority targets`],
  [/^@ 全工作区 \((\d+)\/(\d+)\)$/, (_, a, b) => `@ All Workspace (${a}/${b})`],
  [/^(\d+) 条 · 当前 (.+)$/, (_, n, u) => `${n} msgs · Current: ${u}`],
  [/^目标:\s*(.+)$/, (_, n) => `Target: ${n}`],
  [/^目标状态:\s*(.+)$/, (_, s) => `Target State: ${txRoleOrLabel(s)}`],
  [/^工作区在线\s*(\d+)\/(\d+)\s*台主机$/, (_, a, b) => `${a}/${b} hosts online`],
  [/^工作区在线主机:\s*(.+)$/, (_, s) => `Workspace Online: ${s}`],
  [/^### (.+?) · 上下文分析$/, (_, n) => `### ${n} · Context Analysis`],
  [/^- \*\*范围\*\*：(.+)$/, (_, s) => `- **Scope**: ${txRoleOrLabel(s)}`],
  [/^- \*\*问题\*\*：(.+)$/, (_, s) => `- **Prompt**: ${s}`],
  [/^- \*\*任务统计\*\*：共 (\d+) 条记录，执行中 \*\*(\d+)\*\* 条，异常\/失败 \*\*(\d+)\*\* 条$/, (_, tot, run, fail) =>
    `- **Tasks**: ${tot} total, **${run}** running, **${fail}** failed/abnormal`],

  // BOFs / ExtKit
  [/^Linux ELF (\d+) · 可执行 (\d+)(?: · 源码已改 (\d+))?(?: · 未登记 (\d+))?$/, (_, a, b, c, d) =>
    `Linux ELF ${a} · Runnable ${b}${c ? ` · Modified ${c}` : ''}${d ? ` · Unlisted ${d}` : ''}`],
  [/^·\s*源码已改 (\d+)$/, (_, n) => `· Modified ${n}`],
  [/^·\s*未登记 (\d+)$/, (_, n) => `· Unlisted ${n}`],
  [/^(\d+) 子命令$/, (_, n) => `${n} subcommands`],
  [/^子命令 \((\d+)\/(\d+)\)$/, (_, a, b) => `Subcommands (${a}/${b})`],
  [/^子命令 (\d+)$/, (_, n) => `Subcommands ${n}`],
  [/^运行参数$/, () => 'Parameters'],
  [/^离线兜底 (\d+)$/, (_, n) => `Offline fallback ${n}`],
  [/^未找到 (.+?) BOF 插件$/, (_, p) => `No ${p} BOF modules found`],
  [/^当前无可用的 (.+?) BOF 插件$/, (_, p) => `No ${p} BOF modules available`],
  [/^展开 (\d+) 个子命令$/, (_, n) => `Expand ${n} subcommands`],
  [/^ · (\d+) 项$/, (_, n) => ` · ${n} items`],
  [/^已扫描 (\d+) 个 \.o$/, (_, n) => `Scanned ${n} .o files`],
  [/^需要一个整数，收到 (.+)$/, (_, v) => `Expected an integer, got ${v}`],
  [/^([a-zA-Z0-9_.-]+)\.o 在服务端不存在，先编译或上传$/, (_, b) => `${b}.o not found on server; compile or upload first`],
  [/^([a-zA-Z0-9_.-]+)\.c 不在服务端，无法重编$/, (_, b) => `${b}.c source missing on server`],
  [/^缺少必填参数：([\s\S]+)$/, (_, p) => `Missing required argument: ${p}`],
  [/^参数打包失败：([\s\S]+)$/, (_, e) => `Argument packing failed: ${txRoleOrLabel(e)}`],
  [/^BOF (.+?) 已下发 → (.+)$/, (_, b, c) => `BOF ${b} dispatched → ${c}`],
  [/^BOF (.+?) 下发失败$/, (_, b) => `BOF ${b} failed to dispatch`],
  [/^自定义 COFF BOF 已下发 → (.+)$/, (_, c) => `Custom COFF BOF dispatched → ${c}`],
  [/^(.+?) BOF (.+?) 已下发 → (.+)$/, (_, p, b, c) => `${p} BOF ${b} dispatched → ${c}`],
  [/^(.+?) 下发失败:\s*([\s\S]+)$/, (_, c, e) => `${c} dispatch failed: ${txRoleOrLabel(e)}`],
  [/^(.+?) 下发失败$/, (_, c) => `${c} dispatch failed`],
  [/^(.+?)\.c 已上传并编译$/, (_, n) => `${n}.c uploaded and compiled`],
  [/^(.+?)\.c 已上传，但编译失败（见下方输出）$/, (_, n) => `${n}.c uploaded, compilation failed (see output below)`],
  [/^(.+?)\.c 已上传，但编译失败$/, (_, n) => `${n}.c uploaded, compilation failed`],
  [/^(.+?)\.o 已上传$/, (_, n) => `${n}.o uploaded`],
  [/^(.+?) 已重编（产物有变化）$/, (_, n) => `${n} rebuilt (binary updated)`],
  [/^(.+?) 已重编（与现有产物字节全同）$/, (_, n) => `${n} rebuilt (identical binary)`],
  [/^(.+?) 编译输出$/, (_, n) => `${n} compile output`],
  [/^(.+?) 编译失败$/, (_, n) => `${n} compile failed`],
  [/^全量重编：(\d+)\/(\d+) 成功，(\d+) 个产物有变化$/, (_, a, b, c) => `Rebuild All: ${a}/${b} succeeded, ${c} updated`],
  [/^全量重编：(\d+) 个失败$/, (_, n) => `Rebuild All: ${n} failed`],
  [/^当前目标主机是 (.+?)，请先切换匹配的在线主机$/, (_, p) => `Selected session is ${p}; switch to a matching online session first`],
  [/^当前插件属于 (.+?)，与所选主机 (.+?) 不匹配$/, (_, b, a) => `Module targets ${b}, incompatible with selected agent ${a}`],
  [/^勾上则原样发出 (.+)$/, (_, p) => `Send raw flag ${p}`],
  [/^在 (.+?) 上执行 \(Ctrl\+Enter\)$/, (_, a) => `Execute on ${a} (Ctrl+Enter)`],
  [/^当前主机是 (.+?) agent，本页显示的是 (.+?) 的命令$/, (_, a, b) => `Target session is ${a}, while showing ${b} commands`],
  [/^目标是 (.+?) agent，本页显示的是 (.+?) 的命令$/, (_, a, b) => `Target is ${a}; viewing ${b} commands`],
  [/^(.+?) 需要子命令或参数，请照示例改好再发$/, (_, c) => `${c} requires a subcommand or args (see examples)`],
  [/^扩展命令 (.+?) 已下发$/, (_, c) => `Dispatched extension command: ${c}`],
  [/^(.+?) 注册的命令集（(\d+) 条）$/, (_, a, n) => `${a} commands (${n})`],
  [/^按用途归类的套件：(.+)$/, (_, g) => `Suites by category: ${g}`],
  [/^没有命令匹配「(.+?)」$/, (_, q) => `No commands match "${q}"`],
  [/^有 (\d+) 个子命令，必须挑一个 —— 所以这条不能一键下发，照示例写$/, (_, n) => `${n} subcommands available — pick one using the examples`],
  [/^(\d+) 个参数，其中 (\d+) 个必填$/, (_, a, b) => `${a} arg${a === '1' ? '' : 's'} (${b} required)`],
  [/^(.+?) 的批次里没有这条命令$/, (_, m) => `Unavailable in ${m} batches`],
  [/^复制命令名：(.+)$/, (_, n) => `Copy command: ${n}`],
  [/^在 (.+?) 上执行 (.+?)（没显式选主机，发往第一台在线主机）$/, (_, t, c) => `Run ${c} on ${t} (defaulting to 1st online session)`],
  [/^在 (.+?) 上执行 (.+)$/, (_, t, c) => `Run ${c} on ${t}`],
  [/^套件 (\d+) · 命令 (\d+) · 含子命令 (\d+) 项 · AxScript PreHook 执行$/, (_, s, c, l) =>
    `Suites ${s} · Commands ${c} · ${l} subcommands · AxScript PreHook`],

  // Files / Processes / PTY / Downloads / Tunnels / Hosts / Relay / Store
  [/^已下发，等待回显 (\d+)s…$/, (_, s) => `Sent, awaiting output (${s}s)…`],
  [/^已下发 cat，等待回显 (\d+)s…$/, (_, s) => `Sent cat, awaiting output (${s}s)…`],
  [/^轮询任务 (\d+)s…$/, (_, s) => `Polling task (${s}s)…`],
  [/^等待主机回显 (\d+)s$/, (_, s) => `Waiting for agent (${s}s)`],
  [/^查询结果 (\d+)s$/, (_, s) => `Fetching result (${s}s)`],
  [/^文件 (.+?):\s*(\d+)$/, (_, p, n) => `Files ${p}: ${n}`],
  [/^进程 (.+?):\s*(\d+)$/, (_, p, n) => `Processes ${p}: ${n}`],
  [/^已下发下载：(.+)$/, (_, n) => `Download queued: ${n}`],
  [/^已读取 (.+)$/, (_, n) => `Loaded ${n}`],
  [/^确认删除？\n([\s\S]+)$/, (_, p) => `Confirm delete?\n${p}`],
  [/^确认删除选中的 (\d+) 项？$/, (_, n) => `Delete ${n} selected items?`],
  [/^已请求删除 (\d+) 项$/, (_, n) => `Delete requested for ${n} items`],
  [/^已请求删除 (.+)$/, (_, n) => `Delete requested: ${n}`],
  [/^已下发 (\d+) 个(下载|上传)任务$/, (_, n, k) => `${n} ${k === '下载' ? 'download' : 'upload'} tasks queued`],
  [/^上传 (.+?) · (\d+)%$/, (_, n, p) => `Uploading ${n} · ${p}%`],
  [/^上传任务已下发:\s*(.+?) → (.+)$/, (_, n, r) => `Upload queued: ${n} → ${r}`],
  [/^上传失败:\s*([\s\S]+)$/, (_, e) => `Upload failed: ${txRoleOrLabel(e)}`],
  [/^已请求结束 PID (\d+)$/, (_, p) => `Kill requested for PID ${p}`],
  [/^已结束 PID (\d+)$/, (_, p) => `Killed PID ${p}`],
  [/^结束进程？\nPID (\d+)\n([\s\S]+)$/, (_, p, n) => `Kill process?\nPID ${p}\n${n}`],
  [/^警告：PID (\d+) \((.+?)\) 是当前会话自身进程！结束会导致会话离线。\n确认结束？$/, (_, p, n) =>
    `Warning: PID ${p} (${n}) is the current agent process! Killing it terminates this session.\nConfirm?`],
  [/^输入命令开始执行 · (.+?) · ↑↓ 历史 · Tab 补全$/, (_, a) => `Type command · ${a === '当前 agent' ? 'current agent' : a} · ↑↓ history · Tab complete`],
  [/^重放 (.+)$/, (_, i) => `Replay ${i}`],
  [/^PTY 通道已建立 ([^ ]+) (.+)$/, (_, a, p) => `PTY opened on ${a}: ${p}`],
  [/^PTY OTP 失败:\s*([\s\S]+)$/, (_, e) => `PTY OTP failed: ${txRoleOrLabel(e)}`],
  [/^PTY WebSocket 创建失败:\s*([\s\S]+)$/, (_, e) => `PTY WebSocket init failed: ${txRoleOrLabel(e)}`],
  [/^PTY WebSocket 连接超时\n([\s\S]+)$/, (_, u) => `PTY WebSocket timed out\n${u}`],
  [/^PTY 通道被关闭 \(code=(\d+)\)，请确认主机在线且已 sleep 0$/, (_, c) => `PTY closed (code=${c}) — verify host online & sleep 0`],
  [/^该主机没有可用的「(.+?)」命令（(.+?)）$/, (_, op, ctx) => `No "${op}" command available on this agent (${ctx})`],
  [/^路径异常（盘符重复）:\s*(.+)$/, (_, p) => `Invalid path (duplicate drive letter): ${p}`],
  [/^删除主机 (.+?)？$/, (_, id) => `Remove agent ${id}?`],
  [/^服务端清除失败，已清本地：([\s\S]+)$/, (_, e) => `Server removal failed, cleared locally: ${txRoleOrLabel(e)}`],
  [/^删除下载记录 (.+?)？$/, (_, id) => `Delete download ${id}?`],
  [/^已复制 (.+?) 模板$/, (_, k) => `Copied ${k} snippet`],
  [/^已停止隧道 (.+?)？（配置保留，可再启用）$/, (_, id) => `Stop tunnel ${id}? (config preserved)`],
  [/^停止隧道 (.+?)？（配置保留，可再启用）$/, (_, id) => `Stop tunnel ${id}? (config preserved)`],
  [/^隧道已停止 (.+)$/, (_, id) => `Tunnel stopped: ${id}`],
  [/^隧道已启用 (.+)$/, (_, id) => `Tunnel enabled: ${id}`],
  [/^删除隧道 (.+?)？此操作不可恢复$/, (_, id) => `Delete tunnel ${id}? This cannot be undone`],
  [/^隧道已删除(?:\(stop\))? (.+)$/, (_, id) => `Tunnel deleted: ${id}`],
  [/^隧道信息已更新 (.+)$/, (_, id) => `Tunnel updated: ${id}`],
  [/^隧道已创建 ?(.*)$/, (_, m) => `Tunnel created ${m}`.trim()],
  [/^主机 ([^ ]+) 已是 sleep 0$/, (_, a) => `Agent ${a} already at sleep 0`],
  [/^已加速主机 ([^（]+)（sleep 0），关闭文件\/进程页签后恢复 (.+?)s$/, (_, a, s) => `Boosted ${a} (sleep 0); restores ${s}s on close`],
  [/^交互加速：sleep 0（关闭后恢复 (.+?)s）$/, (_, s) => `Interactive boost: sleep 0 (restores ${s}s on close)`],
  [/^加速失败:\s*([\s\S]+)$/, (_, e) => `Boost failed: ${txRoleOrLabel(e)}`],
  [/^已恢复主机 ([^ ]+) sleep (.+?)s(.*)$/, (_, a, s, j) => `Restored ${a} sleep ${s}s${j}`],
  [/^已恢复 sleep (.+?)s$/, (_, s) => `Restored sleep ${s}s`],
  [/^恢复 sleep 失败:\s*([\s\S]+)$/, (_, e) => `Restore sleep failed: ${txRoleOrLabel(e)}`],
  [/^拓扑图加载失败:\s*([\s\S]+)$/, (_, e) => `Topology load failed: ${txRoleOrLabel(e)}`],
  [/^拓扑图保存失败[:：]\s*([\s\S]+)$/, (_, e) => `Topology save failed: ${txRoleOrLabel(e)}`],
  [/^恢复会话 (.+?)…$/, (_, u) => `Restoring session ${u}…`],
  [/^会话已失效，请重新登录:\s*([\s\S]+)$/, (_, e) => `Session expired — please reconnect: ${txRoleOrLabel(e)}`],
  [/^REST 加载失败:\s*([\s\S]+)$/, (_, e) => `REST load failed: ${txRoleOrLabel(e)}`],
  [/^sync 调用失败:\s*([\s\S]+)$/, (_, e) => `sync failed: ${txRoleOrLabel(e)}`],
  [/^WebSocket 关闭 code=(.+)$/, (_, c) => `WebSocket closed (code=${c})`],
  [/^实时通道失败（REST 仍可用）:\s*([\s\S]+)$/, (_, m) => `Realtime channel failed (REST active): ${txRoleOrLabel(m)}`],
  [/^实时通道失败，继续使用 REST:\s*([\s\S]+)$/, (_, m) => `Realtime channel failed, using REST: ${txRoleOrLabel(m)}`],
  [/^命令失败:\s*([\s\S]+)$/, (_, e) => `Command failed: ${txRoleOrLabel(e)}`],
  [/^已请求删除 (\d+) 台主机（服务端报告部分失败）$/, (_, n) => `Delete requested for ${n} agents (partial errors)`],
  [/^已删除 (\d+) 台主机$/, (_, n) => `Deleted ${n} agents`],
  [/^删除未全部成功（本地已清理，稍后对账）：([\s\S]+)$/, (_, e) => `Partial delete failure (local cache cleared): ${txRoleOrLabel(e)}`],
  [/^已(.+?) relay$/, (_, l) => `Relay: ${txRoleOrLabel(l)}`],
  [/^(.+?)失败[：:]\s*([\s\S]+)$/, (_, l, e) => `${txRoleOrLabel(l)} failed: ${txRoleOrLabel(e)}`],
  [/^(.+?)失败$/, (_, l) => `${txRoleOrLabel(l)} failed`],
  [/^（(.+?) 不存在或为空）$/, (_, f) => `(${f} missing or empty)`],
  [/^预览失败 HTTP (\d+)$/, (_, c) => `Preview failed HTTP ${c}`],
  [/^（仅 ([^）]+)）$/, (_, a) => `(${a} only)`],
]

/**
 * Longest-first phrase glossary for any unforeseen server-provided strings
 * (e.g. dynamic `.axs` BuildSchema fields, labels, or error responses).
 */
const PHRASE_GLOSSARY: Array<[string, string]> = [
  ['工作区实时运行状态', 'Workspace live status'],
  ['多回连地址轮换', 'Callback rotation'],
  ['反调试', 'Anti-debug'],
  ['字符串混淆', 'String obfuscation'],
  ['自毁日期', 'KillDate'],
  ['工作时间窗', 'Working hours'],
  ['重连间隔', 'Reconnect interval'],
  ['重连次数', 'Reconnect retries'],
  ['目标平台', 'Target platform'],
  ['目标主机', 'Target agent'],
  ['目标架构', 'Target arch'],
  ['回连地址', 'Callback address'],
  ['绑定地址', 'Bind address'],
  ['监听地址', 'Listen address'],
  ['监听器', 'Listener'],
  ['监听器名称', 'Listener name'],
  ['代理协议', 'Proxy protocol'],
  ['代理端口', 'Proxy port'],
  ['代理服务器', 'Proxy host'],
  ['用户名', 'Username'],
  ['口令', 'Password'],
  ['密码', 'Password'],
  ['服务端', 'Server'],
  ['客户端', 'Client'],
  ['可执行文件', 'Executable'],
  ['动态库', 'Shared library'],
  ['导出函数', 'Export forwarders'],
  ['自签证书', 'Self-signed cert'],
  ['伪装', 'Masquerade'],
  ['侧载', 'Sideloading'],
  ['端口', 'Port'],
  ['地址', 'Address'],
  ['路径', 'Path'],
  ['文件', 'File'],
  ['目录', 'Directory'],
  ['协议', 'Protocol'],
  ['间隔', 'Interval'],
  ['抖动', 'Jitter'],
  ['超时', 'Timeout'],
  ['启用', 'Enable'],
  ['禁用', 'Disable'],
  ['可选', 'Optional'],
  ['必填', 'Required'],
  ['留空', 'Leave blank'],
  ['默认', 'Default'],
  ['参数', 'Parameters'],
  ['选项', 'Options'],
  ['配置', 'Config'],
  ['说明', 'Notes'],
  ['提示', 'Tip'],
  ['警告', 'Warning'],
  ['错误', 'Error'],
  ['失败', 'Failed'],
  ['成功', 'Succeeded'],
  ['未知', 'Unknown'],
  ['在线', 'Online'],
  ['离线', 'Offline'],
  ['加载中', 'Loading'],
  ['生成', 'Build'],
  ['投递', 'Delivery'],
  ['刷新', 'Refresh'],
  ['复制', 'Copy'],
  ['删除', 'Delete'],
  ['取消', 'Cancel'],
  ['关闭', 'Close'],
  ['确定', 'OK'],
  ['保存', 'Save'],
  ['应用', 'Apply'],
  ['重置', 'Reset'],
  ['执行', 'Execute'],
  ['主机', 'Host'],
  ['进程', 'Process'],
  ['隧道', 'Tunnel'],
  ['下载', 'Download'],
  ['上传', 'Upload'],
  ['载荷', 'Payload'],
  ['模板', 'Template'],
  ['脚本', 'Script'],
  ['命令', 'Command'],
  ['任务', 'Task'],
  ['日志', 'Log'],
  ['状态', 'Status'],
  ['名称', 'Name'],
  ['类型', 'Type'],
  ['时间', 'Time'],
  ['版本', 'Version'],
  ['图标', 'Icon'],
  ['签名', 'Signature'],
  ['加密', 'Encryption'],
  ['证书', 'Certificate'],
  ['代理', 'Proxy'],
  ['管道', 'Pipe'],
  ['域名', 'Domain'],
  ['字节', 'Bytes'],
  ['全工作区', 'All Workspace'],
  ['工作区', 'Workspace'],
  ['在线主机', 'online hosts'],
  ['分析', 'analysis'],
  ['分支', 'branch'],
  ['区分', 'distinguish'],
  ['标签', 'Tag'],
  ['毫秒', 'ms'],
  ['小时', 'h'],
]

function applyGlossaryFallback(input: string): string {
  let out = input
  for (const [zh, en] of PHRASE_GLOSSARY) {
    if (out.includes(zh)) {
      out = out.split(zh).join(` ${en} `)
    }
  }
  out = out
    .replace(/（/g, ' (')
    .replace(/）/g, ') ')
    .replace(/【/g, ' [')
    .replace(/】/g, '] ')
    .replace(/「|『/g, '"')
    .replace(/」|』/g, '"')
    .replace(/：/g, ': ')
    .replace(/，/g, ', ')
    .replace(/；/g, '; ')
    .replace(/。/g, '. ')
    .replace(/、/g, ', ')
    .replace(/——/g, ' — ')
    .replace(/[ \t]{2,}/g, ' ')
    .trim()
  return out
}

function translateInner(s: string, depth = 0): string {
  if (!s || !ZH_RE.test(s)) return s

  // 1. Direct / whitespace / punctuation-normalized lookup
  const hit = findInDict(s)
  if (hit !== undefined) return hit

  const norm = normWS(s)

  // 3. Punctuation wrappers / trailing markers
  if (s.endsWith('：') || s.endsWith(':')) {
    const core = s.slice(0, -1).trim()
    if (EN_DICT[core] !== undefined) return `${EN_DICT[core]}:`
  }
  if (s.endsWith('…')) {
    const core = s.slice(0, -1).trim()
    if (EN_DICT[core] !== undefined) return `${EN_DICT[core]}…`
  }
  if (s.startsWith('（') && s.endsWith('）')) {
    const inner = s.slice(1, -1).trim()
    const tx = translateInner(inner, depth + 1)
    if (!ZH_RE.test(tx)) return `(${tx})`
  }
  if ((s.startsWith('「') && s.endsWith('」')) || (s.startsWith('【') && s.endsWith('】'))) {
    const inner = s.slice(1, -1).trim()
    const tx = translateInner(inner, depth + 1)
    if (!ZH_RE.test(tx)) return s.startsWith('【') ? `[${tx}]` : `"${tx}"`
  }

  // 4. Dynamic regex templates
  for (const [re, fn] of DYNAMIC_RULES) {
    const m = re.exec(s) || (norm !== s ? re.exec(norm) : null)
    if (m) {
      return fn(...m)
    }
  }

  // 5. Recursive multi-line / clause splitting (bounded recursion)
  if (depth < 3) {
    if (s.includes('\n')) {
      const lines = s.split('\n').map((ln) => translateInner(ln, depth + 1))
      return lines.join('\n')
    }
    for (const sep of [' · ', ' — ', ' —— ', ' / ', '；', '，'] as const) {
      if (s.includes(sep)) {
        const outSep = sep === '，' ? ', ' : sep === '；' ? '; ' : sep === ' —— ' ? ' — ' : sep
        const parts = s.split(sep).map((p) => {
          const t = p.trim()
          return t ? translateInner(t, depth + 1) : ''
        })
        const joined = parts.join(outSep)
        if (!ZH_RE.test(joined)) return joined
      }
    }
  }

  // 6. Final glossary cleanup for unanticipated dynamic strings (e.g. server schemas)
  return applyGlossaryFallback(norm)
}

export function translateEn(raw: string): string {
  if (!raw || !ZH_RE.test(raw)) return raw
  const cached = CACHE.get(raw)
  if (cached !== undefined) return cached

  const direct = EN_DICT[raw]
  if (direct !== undefined) return setCache(raw, direct)

  const wsMatch = /^(\s*)([\s\S]*?)(\s*)$/.exec(raw)
  if (!wsMatch) return raw
  const [, lead, body, trail] = wsMatch
  const translatedBody = translateInner(body, 0)
  return setCache(raw, `${lead}${translatedBody}${trail}`)
}
