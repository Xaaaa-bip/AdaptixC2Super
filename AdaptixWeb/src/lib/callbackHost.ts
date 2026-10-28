/**
 * 「回连地址」缺省主机名的取值规则。
 *
 * ★ 这里**绝不能**写死一个 IP。此前 defaultListenerConfig 的缺省值是上游作者自己的机器
 *   （上游作者的IP），在别的部署里新建监听器会把它填进去 —— 而回连地址是**目标机**要拨
 *   的地址，填错的后果是生成的 agent 永远不回连，界面上却看不出任何异常（字段看着是填好
 *   的）。Qt 侧对这个字段干脆**不预填**（只放占位串 `address:port`），正是因为「目标机该
 *   从哪个地址回连」只有操作员知道。
 *
 * 所以这里的原则是：能推的用**运行期事实**推，推不出就给空串让操作员自己填（提交时会拦），
 * 并且在界面上把服务端自报的全部地址摆出来供他改 —— 不制造第二个「看着对其实是错的」缺省。
 *
 * ── 事实来源：同步起始包里的 `interfaces` ──────────────────────────────────────
 * 它由 core/server/server.go:190-205 的 Start() 填成 `["0.0.0.0"] + 每个网卡的每个 IPv4`。
 * 两个后果必须记住：
 *   · **恒定含通配地址 "0.0.0.0"**（net.Interfaces() 成功就无条件 append 一条）。
 *     通配地址只能 bind、不能被拨，把它当回连地址填出去等于让 agent 连 0.0.0.0 —— 比原来的
 *     作者 IP 还糟（本文件第一版就是这么翻车的：线上实测缺省值变成了 `0.0.0.0:443`）。
 *   · **只列网卡上的地址**。服务端在 NAT 后面时，公网地址不在任何网卡上 ⇒ 这份列表里
 *     根本不会有它（本机实测只有 `0.0.0.0 127.0.0.1 10.7.34.40`，而操作员是从
 *     154.198.162.34 访问进来的）。所以**必须**把「操作员当前访问控制台用的主机名」也算作
 *     一个独立的候选来源。
 * 注意这与 Qt 的 ax.interfaces() **不是同一个列表**：那个走 core/server/ts_axscript.go:760
 * 的 AxGetInterfaces()，会滤掉回环、不含通配、且带 IPv6。Web 这边拿不到它（connector 未暴露
 * 对应路由），只能用同步包这一份 —— 故两者行为有差异是**已知**的，不是 bug。
 *
 * ── 取值优先级 ────────────────────────────────────────────────────────────────
 *   1. 操作员访问控制台用的主机名，且它出现在服务端自报列表里。
 *      两边对上 ⇒ 既是服务端真实持有的地址，又已被证明可达，最可信。
 *   2. 自报列表里的第一个公网 IPv4（跳掉通配/回环/链路本地/内网段）。
 *      覆盖「操作员在内网看控制台、目标机在公网」这种最常见的不一致。
 *   3. 操作员访问控制台用的主机名（非本机回环）。
 *      覆盖「服务端在 NAT 后面」：此时列表里全是内网地址，而操作员进来的那个地址
 *      （公网 IP 或域名）恰恰是唯一被证明能从外面拨通的。★ 可能是域名，而 **linux agent
 *      没有 DNS 解析器**，填域名会静默失败；但 Windows beacon / kharon 能解析，且提示行
 *      已把所有候选摆出来，操作员改一下即可。
 *   4. 自报列表里的第一个 IPv4（内网地址也算）。同网段目标机可用。
 *   5. 都没有 ⇒ 空串，交给操作员填（提交时拦）。
 */

/** 形如 a.b.c.d 的 IPv4 字面量（不做范围校验，范围问题交给下面的 isPrivateIPv4）。 */
const IPV4 = /^\d{1,3}(\.\d{1,3}){3}$/

/** 只能 bind、不能被拨的地址。 */
function isWildcard(s: string): boolean {
  return s === '0.0.0.0' || s === '*' || s === '::' || s === '[::]'
}

/**
 * 服务端自报列表里**可以拿去当回连地址候选**的那些：滤掉通配与空串。
 * 提示行与取值规则共用它，免得两处过滤条件漂移。
 * （回环保留 —— 同机实验环境里它确实是有效答案，排序时靠 isPrivateIPv4 压到最后。）
 */
export function dialableInterfaces(serverInterfaces: string[]): string[] {
  return (serverInterfaces || []).filter((x) => typeof x === 'string' && x.trim() !== '' && !isWildcard(x.trim()))
}

/** 私有 / 回环 / 链路本地 / 运营商级 NAT —— 这些通常不是目标机的回连地址。 */
function isPrivateIPv4(s: string): boolean {
  const p = s.split('.').map(Number)
  if (p.length !== 4 || p.some((n) => !Number.isInteger(n) || n < 0 || n > 255)) return true
  const [a, b] = p
  return (
    a === 10 || // 10/8
    a === 127 || // 回环
    (a === 172 && b >= 16 && b <= 31) || // 172.16/12
    (a === 192 && b === 168) || // 192.168/16
    (a === 169 && b === 254) || // 链路本地
    (a === 100 && b >= 64 && b <= 127) // 100.64/10（CGNAT）
  )
}

/** 操作员访问控制台用的主机名能不能当候选（滤掉本机回环与空串）。 */
function isDialableBrowseHost(host: string): boolean {
  if (!host) return false
  const h = host.toLowerCase()
  if (isWildcard(h) || h === 'localhost' || h === '[::1]' || h === '::1') return false
  if (IPV4.test(h)) {
    const [a, b] = h.split('.').map(Number)
    return a !== 127 && !(a === 169 && b === 254) // 回环 / 链路本地不可用；内网地址保留（①会先把它们接住）
  }
  return true // 域名：可能正是 NAT 后面那个公网入口
}

export function pickCallbackHost(serverInterfaces: string[], browseHost: string): string {
  const ifaces = dialableInterfaces(serverInterfaces)
  const host = (browseHost || '').trim()
  const browseOk = isDialableBrowseHost(host)

  // ① 两边对上
  if (browseOk && ifaces.includes(host)) return host

  // ② 自报的公网 IPv4
  const pub = ifaces.find((x) => IPV4.test(x) && !isPrivateIPv4(x))
  if (pub) return pub

  // ③ 操作员进来的那个地址（NAT 后面时它是列表里没有的公网地址）
  if (browseOk) return host

  // ④ 自报的第一个 IPv4
  return ifaces.find((x) => IPV4.test(x)) || ''
}
