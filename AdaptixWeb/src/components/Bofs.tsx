import { memo, useCallback, useEffect, useMemo, useRef, useState } from 'react'
import { useStore } from '../store'
import { t } from '../i18n'
import { loadCmdTree } from '../api/cmdTree'
import type { CmdArgDef, CmdBatch, CmdDef } from '../api/types'
import { bytesToB64, copyText, fileToB64, isAgentOnline, isLinuxAgent, useOnlineClock5s } from '../utils'
import { clearCompletionCache } from '../lib/completions'
import { EmptyState } from './ui'
import { PageTips } from './PageTips'
import { qtAsset } from '../design/qt-themes'

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
    category: 'recon', desc: t('主机信息汇总：内核/发行版/CPU/内存/网卡/用户'),
    notes: [
      '只读、不落盘，任何时候都能安全重跑；回执很长（含用户/组/登录历史/crontab）',
      '拿到的内核版本是选提权路线的输入 —— 接着喂给 kernel_exploit_check',
    ],
  },
  proc_enum: {
    category: 'recon', desc: t('枚举全部进程（PID/PPID/路径/用户）'),
    notes: [
      '只读 /proc：线程 / fd / cmdline / maps / cwd，输出量很大',
      '动手做持久化之前先看这里：有没有 EDR、auditd、抓包进程在盯',
    ],
  },
  net_enum: {
    category: 'recon', desc: t('网络枚举：接口/路由/监听端口'),
    notes: [
      '只读：接口 / 路由 / ARP / DNS / 连接表',
      '横向前的第一步；配合 ebpf_load 的 ports 参数，确认哪些端口已经藏起来',
    ],
  },
  service_enum: {
    category: 'recon', desc: t('服务枚举：systemd 单元/开机项'),
    notes: [
      '只读：systemd 单元 + init 脚本，用来找能借壳的常驻服务',
      '装 systemd_persist 之前先跑一遍，留一份"原本长什么样"的基线',
    ],
  },
  container_detect: {
    category: 'recon', desc: t('容器/虚拟化环境检测'),
    notes: [
      '只读：判断在不在容器/虚拟机里，并给出逃逸线索',
      '容器里做的持久化重启即失 —— 先确认自己在哪一层，再决定往哪落',
    ],
  },
  kernel_exploit_check: {
    category: 'recon', desc: t('内核版本 vs 已知提权 exploit 匹配'),
    notes: [
      '只读：内核版本 vs 内置提权 CVE 表（含 DirtyPipe 等）',
      '★ 回执末尾那行 MATRIX:kver=…|btf=…|kconfig=… 是喂给载荷矩阵解析器的，别丢',
    ],
  },
  // ── 提权 ──
  sudo_check: {
    category: 'priv', desc: t('sudo 配置检查（-l / 无密码项 / env_keep）'),
    notes: [
      '只读解析 sudoers，不改配置；等价于 sudo -l 但不产生 execve',
      '重点看 NOPASSWD 项和 env_keep —— LD_PRELOAD/PYTHONPATH 类可以直接提权',
    ],
  },
  suid_scan: {
    category: 'priv', desc: t('SUID 二进制扫描（GTFOBins 候选）'),
    notes: [
      '只读，但要遍历整个文件系统 —— 慢，建议勾异步 -a',
      '命中的二进制会直接标出 GTFOBins 用法',
    ],
  },
  ld_preload_check: {
    category: 'priv', desc: t('LD_PRELOAD 利用面检查'),
    notes: [
      '只读：LD_PRELOAD 环境变量 + /etc/ld.so.preload + 可疑 .so',
      '装 ld_preload_install 前后各跑一次，确认落点、也确认没被别人占坑',
    ],
  },
  shadow_dump: {
    category: 'priv', desc: t('读取 /etc/shadow（需 root）'),
    notes: [
      '★ 需要 root：非 root 直接报错返回，不会给半截数据',
      '拿到就离线破解；在线爆破会在目标机上留 auth 日志，别在那台机上做',
    ],
  },
  // ── 凭据 ──
  cred_harvest: {
    category: 'cred', desc: t('凭据收集：history/bashrc/env/常见密钥'),
    notes: [
      '★ 要扫全部用户需要 root；非 root 只能看当前用户的',
      '比内置 creds 多扫 terraform / vault / npm / pip / git，并提取云 IMDS 地址',
    ],
  },
  ssh_keys: {
    category: 'cred', desc: t('SSH 私钥/authorized_keys 收集'),
    notes: [
      '★ 要覆盖全部用户需要 root',
      '拿到私钥顺手看 known_hosts —— 那是一张现成的横向地图',
    ],
  },
  // ── 维持 ──
  cron_persist: {
    category: 'persist', desc: t('crontab 持久化（含 timestomp）'),
    notes: [
      '落 /etc/cron.d/<任务名>（默认 sys-update），chmod 0600 且 mtime 对齐目录 mtime',
      '★ 落地产物在"加载之后"才出现，ebpf_load 的 names 覆盖不到 —— 本命令会自己补登记进 hide_names',
      '现代发行版可能根本没装 cron（Ubuntu 24.04 默认无）——先 service_enum 确认',
    ],
    params: [
      { key: 'command', label: t('命令'), placeholder: '/tmp/x', required: true },
      { key: 'job_name', label: '任务名', placeholder: 'agent_update' },
    ],
  },
  ssh_persist: {
    category: 'persist', desc: t('authorized_keys 后门（追加公钥）'),
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
    category: 'persist', desc: t('LD_PRELOAD 劫持安装（.so 需已投递到目标机）'),
    notes: [
      '★ .so 必须**已经投递到目标机**上，本命令只负责把路径写进 /etc/ld.so.preload',
      'ld.so.preload 在发行版里默认不存在，它的出现本身就是 IOC —— 本命令会自己把它登记进 hide_names',
      '只影响之后新起的进程；已经在跑的进程不受影响',
    ],
    params: [{ key: 'so_path', label: '目标机 .so 路径', placeholder: '/tmp/evil.so', required: true }],
  },
  ko_load: {
    category: 'persist', desc: t('内核模块加载（finit_module，.ko 需已投递）'),
    notes: [
      '★ .ko 必须与目标内核 vermagic 匹配（由载荷矩阵挑选），不是随便一个 .ko',
      '需要 CAP_SYS_MODULE；lockdown / 模块签名强制会挡，报错码能区分原因',
      '用 finit_module 直接加载而不是 execve insmod —— 少一条 execve 审计事件',
      '加载成功后自动把模块名登记进 hide_names',
    ],
    params: [{ key: 'ko_path', label: '目标机 .ko 路径', placeholder: '/tmp/rootkit.ko', required: true }],
  },
  ebpf_load: {
    category: 'persist', desc: t('eBPF 程序加载（原始指令流，九个位置参数）'),
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
    category: 'persist', desc: t('systemd timer 持久化单元'),
    notes: [
      '落地三件缺一不可：<名>.service + <名>.timer + timers.target.wants/<名>.timer 符号链接；少一个链接就是"看得见但永远不会跑"',
      '★ 不 execve systemctl enable / daemon-reload（少一条 execve 审计 + 一条 journal 记录）；要本次就生效得自己在目标机上 reload',
      '★ 同名单元已存在时拒绝覆盖 —— 覆盖系统单元会直接破坏目标机',
      '单元文件 mtime 对齐目录，且带 RandomizedDelaySec（真实发行版定时器的常见形态）',
    ],
    params: [
      { key: 'command', label: t('命令'), placeholder: '/tmp/x', required: true },
      { key: 'unit_name', label: '单元名', placeholder: 'sys-maintenance' },
    ],
  },
  // ── 执行 ──
  memfd_exec: {
    category: 'exec', desc: t('无文件执行 ELF（fork 隔离；路径是目标机上的）'),
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
    category: 'exec', desc: t('把 ELF 丢成"无父、无盘、无 fd"的后台进程'),
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
    category: 'exec', desc: t('载荷矩阵查表与完整性校验'),
    notes: [
      '校验模式：<manifest 路径> <sha256> <本地文件> → 打 MATRIX_OK / MATRIX_BAD / MATRIX_MISSING',
      '★ 本地这一侧的 sha256 目前是简化占位实现，真正的完整性闸门在服务端 resolver + 下载通道',
    ],
    params: [
      { key: 'kver', label: t('内核版本'), placeholder: '6.8.0', required: true },
      { key: 'arch', label: t('架构'), placeholder: 'x86_64', required: true },
      { key: 'kind', label: t('类型'), placeholder: 'exploit', required: true },
    ],
  },
  // ── 隐匿 · 撤离 ──
  hide_add: {
    category: 'stealth', desc: t('事后往 hide_names 补写要隐藏的名字'),
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
    category: 'stealth', desc: t('从 hide_names 删回名字（恢复可见 / 回收预算）'),
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
    category: 'stealth', desc: t('eBPF rootkit 的失效保护卸载'),
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
    category: 'stealth', desc: t('现场复原 / 撤离（按显式清单删落盘物）'),
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
    category: 'other', desc: t('参数编解码探针（零副作用）'),
    notes: [
      '只打印参数 blob 的头 4 字节 + 逐个 token；不写盘、不加载 eBPF、不改进程属性',
      '★ 用来判定参数通道有没有 4 字节错位：正常时 head4 应当等于"字段个数"',
    ],
  },
}

const CATS: { key: Cat; label: string }[] = [
  { key: 'recon', label: t('侦察') },
  { key: 'priv', label: '提权' },
  { key: 'cred', label: t('凭据') },
  { key: 'persist', label: '维持' },
  { key: 'stealth', label: '隐匿 · 撤离' },
  { key: 'exec', label: t('执行') },
  { key: 'other', label: '其他' },
]

const UNKNOWN_META: BofMeta = {
  category: 'other',
  desc: t('服务端存在、但本地 META 表里没有描述。可能是新加的，或你自己上传的。'),
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

const RECENT_KEY = 'adaptix.bofs.recent.v1'
const RECENT_EXT_KEY = 'adaptix.bofs.recent_ext.v1'
const FAV_KEY = 'adaptix.bofs.favs.v1'
const FAV_EXT_KEY = 'adaptix.bofs.favs_ext.v1'
const SPLIT_KEY = 'adaptix.bofs.split.v1'
type StatusFilter = 'all' | 'ready' | 'stale' | 'uncompiled' | 'unregistered' | 'recent' | 'fav'
type PlatformKind = 'linux' | 'windows' | 'macos'

const CAT_LABEL: Record<Cat, string> = {
  recon: t('侦察'),
  priv: '提权',
  cred: t('凭据'),
  persist: '维持',
  stealth: '隐匿 · 撤离',
  exec: t('执行'),
  other: '其他',
}

const CAT_COLOR: Record<Cat, string> = {
  recon: '#38bdf8',
  priv: '#f59e0b',
  cred: '#a855f7',
  persist: '#ef4444',
  stealth: '#10b981',
  exec: '#6366f1',
  other: '#94a3b8',
}

const PURPOSE_OF_LC: Record<string, string> = {
  'ad-bof': t('域渗透'),
  'adcs-bof': t('域渗透'),
  'kerbeus-bof': t('域渗透'),
  'sql-bof': t('域渗透'),
  'ldap-bof': t('域渗透'),
  'ad relayinformer': t('域渗透'),
  'mimikatz': t('凭据获取'),
  'creds-bof': t('凭据获取'),
  'cookie-monster': t('凭据获取'),
  'nanodump': t('凭据获取'),
  'elevation-bof': t('提权'),
  'execution-bof': t('执行与注入'),
  'no-consolation': t('执行与注入'),
  'injection-bof': t('执行与注入'),
  'lateralmovement-bof': t('横向移动'),
  'lateralmovement-bof-x': t('横向移动'),
  'postex-bof': t('后渗透'),
  'sal-bof': t('侦察与态势'),
  'sar-bof': t('侦察与态势'),
  'process-bof': t('侦察与态势'),
  'process-bof-x': t('侦察与态势'),
}
const PURPOSE_ORDER = [t('侦察与态势'), t('域渗透'), t('凭据获取'), t('提权'), t('执行与注入'), t('横向移动'), t('后渗透'), t('其他')]
const PURPOSE_COLOR: Record<string, string> = {
  [t('侦察与态势')]: '#38bdf8',
  [t('域渗透')]: '#60a5fa',
  [t('凭据获取')]: '#a855f7',
  [t('提权')]: '#f59e0b',
  [t('执行与注入')]: '#6366f1',
  [t('横向移动')]: '#ec4899',
  [t('后渗透')]: '#10b981',
  [t('其他')]: '#94a3b8',
}
const purposeOf = (kit: string) => PURPOSE_OF_LC[kit.trim().toLowerCase()] || t('其他')

function detectPlatform(a?: any): PlatformKind {
  if (!a) return 'windows'
  if (isLinuxAgent(a)) return 'linux'
  const desc = String(a.a_os_desc || '').toLowerCase()
  const name = String(a.a_name || '').toLowerCase()
  if (a.a_os === 3 || desc.includes('mac') || desc.includes('darwin') || name.startsWith('macos')) return 'macos'
  return 'windows'
}

function platformLabel(p: PlatformKind): string {
  if (p === 'linux') return 'Linux'
  if (p === 'macos') return 'macOS'
  return 'Windows'
}

function bofUnsupportedReason(a?: any): string {
  if (!a) return ''
  const name = String(a.a_name || '').toLowerCase()
  if (name === 'gopher' && a.a_os !== 1) {
    return '当前主机为非 Windows 的 gopher agent，服务端无 BOF 加载器，无法执行 execute bof。'
  }
  if (detectPlatform(a) === 'macos') {
    return '当前主机为 macOS agent，服务端无 BOF 加载器，暂不支持 execute bof。'
  }
  return ''
}

function loadStringList(storageKey: string, limit = 20): string[] {
  try {
    const raw = localStorage.getItem(storageKey)
    const arr = raw ? JSON.parse(raw) : []
    return Array.isArray(arr) ? arr.filter((x): x is string => typeof x === 'string').slice(0, limit) : []
  } catch {
    return []
  }
}

type ExtBofItem = {
  key: string
  suite: string
  scriptName: string
  purpose: string
  batchAgent: string
  cmd: CmdDef
}

function requiredExtArgs(c: CmdDef): CmdArgDef[] {
  return (c.args || []).filter((a) => a.required && !(a.flag && !a.name))
}

function extArgDisplayValue(vals: Record<string, string>, baseKey: string, a: CmdArgDef, idx: number): string {
  const k = `${baseKey}.${idx}`
  if (k in vals) return vals[k]
  return a.default_used && a.default_value != null ? String(a.default_value) : ''
}

function buildExtCmdline(
  item: ExtBofItem,
  subName: string,
  vals: Record<string, string>,
): string {
  const subs = item.cmd.subcommands || []
  const sub = subs.length ? (subs.find((s) => s.name === subName) || subs[0]) : null
  const active = sub || item.cmd
  const parts = [item.cmd.name]
  if (sub) parts.push(sub.name)
  const baseKey = `${item.key}:${sub ? sub.name : ''}`
  const args = active.args || []
  args.forEach((a, idx) => {
    const k = `${baseKey}.${idx}`
    if (a.flag && !a.name) {
      if (vals[k] === '1') {
        const flagTok = (a.mark || '').trim()
        if (flagTok) parts.push(flagTok)
      }
      return
    }
    const raw = extArgDisplayValue(vals, baseKey, a, idx).trim()
    if (!raw) return
    const formatted = /\s/.test(raw) && !/^".*"$/.test(raw) ? `"${raw}"` : raw
    if (a.flag && a.name) {
      const m = (a.mark || `--${a.name}`).trim()
      if (m) parts.push(m)
      parts.push(formatted)
    } else {
      parts.push(formatted)
    }
  })
  return parts.join(' ')
}

type ExtListPaneProps = {
  grouped: Array<{ purpose: string; items: ExtBofItem[] }>
  selectedKey: string
  selectedSubName: string
  searchQuery: string
  recentSet: Set<string>
  favSet: Set<string>
  collapsedSet: Set<string>
  expandedSet: Set<string>
  splitPct: number
  hasAny: boolean
  platform: PlatformKind
  onSelect: (key: string) => void
  onSelectSub: (key: string, subName: string) => void
  onToggleCollapse: (group: string) => void
  onToggleExpand: (key: string) => void
  onToggleFav: (key: string) => void
  onContext: (x: number, y: number, item: ExtBofItem) => void
  onResetFilters: () => void
}

const ExtBofListPane = memo(function ExtBofListPane({
  grouped, selectedKey, selectedSubName, searchQuery, recentSet, favSet,
  collapsedSet, expandedSet, splitPct, hasAny, platform,
  onSelect, onSelectSub, onToggleCollapse, onToggleExpand, onToggleFav, onContext, onResetFilters,
}: ExtListPaneProps) {
  const cmdTotal = grouped.reduce((n, g) => n + g.items.length, 0)
  const leafTotal = grouped.reduce((n, g) => n + g.items.reduce((m, it) => m + Math.max(1, (it.cmd.subcommands || []).length), 0), 0)
  const q = searchQuery.trim().toLowerCase()
  return (
    <div className="qt-bof-list-pane" style={{ width: `${splitPct}%` }}>
      <div className="qt-bof-list-hd">
        <span>{platformLabel(platform)} BOF 插件清单</span>
        <span className="mono">{cmdTotal} 命令{leafTotal > cmdTotal ? ` · ${leafTotal} 项` : ''}</span>
      </div>
      <div className="qt-bof-list-scroll">
        {grouped.map((g) => {
          const isCollapsed = !q && collapsedSet.has(`ext:${g.purpose}`)
          const gLeafCount = g.items.reduce((m, it) => m + Math.max(1, (it.cmd.subcommands || []).length), 0)
          return (
            <div key={g.purpose}>
              <button
                type="button"
                className="qt-bof-cat-hd"
                onClick={() => onToggleCollapse(`ext:${g.purpose}`)}
                title="点击折叠 / 展开该分类"
              >
                <span className="qt-bof-cat-left">
                  <span className="qt-bof-cat-dot" style={{ background: PURPOSE_COLOR[g.purpose] || '#94a3b8' }} />
                  <span>{isCollapsed ? '▸' : '▾'}</span>
                  <span>{g.purpose}</span>
                </span>
                <span>{g.items.length}{gLeafCount > g.items.length ? ` (${gLeafCount})` : ''}</span>
              </button>
              {!isCollapsed && g.items.map((it) => {
                const isSel = it.key === selectedKey
                const isRecent = recentSet.has(it.key)
                const isFav = favSet.has(it.key)
                const subs = it.cmd.subcommands || []
                const subCount = subs.length
                const matchingSubs = q
                  ? subs.filter((s) => `${s.name} ${s.description || ''} ${s.example || ''}`.toLowerCase().includes(q))
                  : subs
                const showSubs = subCount > 0 && (expandedSet.has(it.key) || (!!q && matchingSubs.length > 0))
                return (
                  <div key={it.key}>
                    <div
                      role="button"
                      tabIndex={-1}
                      data-bof-name={it.key}
                      className={`qt-bof-row ${isSel ? 'selected' : ''}`}
                      onClick={() => onSelect(it.key)}
                      onContextMenu={(e) => { e.preventDefault(); onSelect(it.key); onContext(e.clientX, e.clientY, it) }}
                    >
                      <div className="qt-bof-row-top">
                        {subCount > 0 && (
                          <button
                            type="button"
                            className="qt-bof-expand-btn"
                            title={showSubs ? '收起子命令' : `展开 ${subCount} 个子命令`}
                            onClick={(e) => { e.stopPropagation(); onToggleExpand(it.key) }}
                          >
                            {showSubs ? '▾' : '▸'}
                          </button>
                        )}
                        <button
                          type="button"
                          className={`qt-bof-star ${isFav ? 'on' : ''}`}
                          title={isFav ? '取消收藏' : '收藏常用 BOF'}
                          onClick={(e) => { e.stopPropagation(); onToggleFav(it.key) }}
                        >
                          {isFav ? '★' : '☆'}
                        </button>
                        {isRecent && !isFav && <span className="qt-bof-row-recent" title="最近执行">●</span>}
                        <span className="qt-bof-row-name">{it.cmd.name}</span>
                        <div className="qt-bof-row-right">
                          {subCount > 0 && <span className="qt-bof-badge warn">{subCount} 子命令</span>}
                          <span className="qt-bof-badge">{it.suite}</span>
                        </div>
                      </div>
                      <div className="qt-bof-row-desc">{it.cmd.description || it.cmd.message || it.cmd.example}</div>
                    </div>
                    {showSubs && matchingSubs.map((sub) => (
                      <button
                        key={sub.name}
                        type="button"
                        className={`qt-bof-subrow ${isSel && selectedSubName === sub.name ? 'selected' : ''}`}
                        onClick={() => onSelectSub(it.key, sub.name)}
                      >
                        <span className="qt-bof-subrow-name">{sub.name}</span>
                        <span className="qt-bof-subrow-desc">{sub.description || sub.example}</span>
                      </button>
                    ))}
                  </div>
                )
              })}
            </div>
          )
        })}
        {cmdTotal === 0 && (
          <div style={{ padding: 16 }}>
            <EmptyState
              icon="⎔"
              title={hasAny ? t('无匹配插件') : `当前无可用的 ${platformLabel(platform)} BOF 插件`}
              hint={hasAny
                ? t('请尝试切换用途分类、套件筛选或清空搜索词')
                : '请确认团队端 profile 已加载 Extension-Kit（.axs 脚本与 *_bin/*.o 产物），点击右上角「刷新」重新拉取命令树'}
              action={hasAny ? (
                <button type="button" className="ghost" onClick={onResetFilters}>重置筛选</button>
              ) : undefined}
            />
          </div>
        )}
      </div>
    </div>
  )
})

type BofListPaneProps = {
  grouped: Array<{ cat: Cat; label: string; items: BofRow[] }>
  selectedName: string
  searchQuery: string
  recentSet: Set<string>
  favSet: Set<string>
  collapsedSet: Set<string>
  offline: boolean
  splitPct: number
  hasRows: boolean
  onSelect: (name: string) => void
  onQuickRun: (row: BofRow) => void
  onToggleCollapse: (group: string) => void
  onToggleFav: (name: string) => void
  onContext: (x: number, y: number, row: BofRow) => void
  onResetFilters: () => void
}

const BofListPane = memo(function BofListPane({
  grouped, selectedName, searchQuery, recentSet, favSet, collapsedSet,
  offline, splitPct, hasRows,
  onSelect, onQuickRun, onToggleCollapse, onToggleFav, onContext, onResetFilters,
}: BofListPaneProps) {
  const totalShown = grouped.reduce((n, g) => n + g.items.length, 0)
  const q = searchQuery.trim().toLowerCase()
  return (
    <div className="qt-bof-list-pane" style={{ width: `${splitPct}%` }}>
      <div className="qt-bof-list-hd">
        <span>Linux ELF BOF 清单</span>
        <span className="mono">{totalShown} 项</span>
      </div>
      <div className="qt-bof-list-scroll">
        {grouped.map((g) => {
          const isCollapsed = !q && collapsedSet.has(`lin:${g.cat}`)
          return (
            <div key={g.cat}>
              <button
                type="button"
                className="qt-bof-cat-hd"
                onClick={() => onToggleCollapse(`lin:${g.cat}`)}
                title="点击折叠 / 展开该分类"
              >
                <span className="qt-bof-cat-left">
                  <span className="qt-bof-cat-dot" style={{ background: CAT_COLOR[g.cat] }} />
                  <span>{isCollapsed ? '▸' : '▾'}</span>
                  <span>{g.label}</span>
                </span>
                <span>{g.items.length}</span>
              </button>
              {!isCollapsed && g.items.map((b) => {
                const isSel = b.name === selectedName
                const isRecent = recentSet.has(b.name)
                const isFav = favSet.has(b.name)
                const pCnt = (b.meta.params || []).length
                return (
                  <div
                    key={b.name}
                    role="button"
                    tabIndex={-1}
                    data-bof-name={b.name}
                    className={`qt-bof-row ${isSel ? 'selected' : ''}`}
                    onClick={() => onSelect(b.name)}
                    onDoubleClick={() => { if (!(b.meta.params || []).some((p) => p.required)) onQuickRun(b) }}
                    onContextMenu={(e) => { e.preventDefault(); onSelect(b.name); onContext(e.clientX, e.clientY, b) }}
                  >
                    <div className="qt-bof-row-top">
                      <button
                        type="button"
                        className={`qt-bof-star ${isFav ? 'on' : ''}`}
                        title={isFav ? '取消收藏' : '收藏常用 BOF'}
                        onClick={(e) => { e.stopPropagation(); onToggleFav(b.name) }}
                      >
                        {isFav ? '★' : '☆'}
                      </button>
                      {isRecent && !isFav && <span className="qt-bof-row-recent" title="最近执行">●</span>}
                      <span className="qt-bof-row-name">{b.name}</span>
                      <div className="qt-bof-row-right">
                        <span className="qt-bof-badge">{pCnt ? `${pCnt} 参` : '无参'}</span>
                        {!b.known && <span className="qt-bof-badge info">{t('未登记')}</span>}
                        {!offline && !b.obj && <span className="qt-bof-badge err">{t('未编译')}</span>}
                        {!offline && b.obj && b.stale && <span className="qt-bof-badge warn">{t('源码已改')}</span>}
                        {!offline && b.obj && !b.stale && <span className="qt-bof-badge ok">就绪</span>}
                        {!offline && b.obj && b.obj_size > 0 && (
                          <span className="qt-bof-row-size">{fmtSize(b.obj_size)}</span>
                        )}
                      </div>
                    </div>
                    <div className="qt-bof-row-desc">{b.meta.desc}</div>
                  </div>
                )
              })}
            </div>
          )
        })}
        {totalShown === 0 && (
          <div style={{ padding: 16 }}>
            <EmptyState
              icon="⎔"
              title={hasRows ? t('无匹配 BOF') : t('服务端还没有 BOF')}
              hint={hasRows ? t('请尝试切换分类、状态或清空搜索词') : t('支持直接拖拽 .c / .o 到此处或点击上方「上传 BOF」')}
              action={hasRows ? (
                <button type="button" className="ghost" onClick={onResetFilters}>重置筛选</button>
              ) : undefined}
            />
          </div>
        )}
      </div>
    </div>
  )
})

export const Bofs = memo(function Bofs() {
  const agents = useStore((s) => s.agents) || {}
  const api = useStore((s) => s.api)
  const selectedAgentId = useStore((s) => s.selectedAgentId)
  const sendCommand = useStore((s) => s.sendCommand)
  const openConsole = useStore((s) => s.openConsole)
  const pushToast = useStore((s) => s.pushToast)
  const pushLog = useStore((s) => s.pushLog)

  const onlineClock = useOnlineClock5s()
  const onlineAgents = useMemo(
    () => {
      void onlineClock
      return Object.values(agents).filter((a: any) => isAgentOnline(a))
    },
    [agents, onlineClock],
  )
  const [agentId, setAgentId] = useState('')
  const effectiveAgent = agentId || (selectedAgentId && isAgentOnline(agents[selectedAgentId]) ? selectedAgentId : '') || (onlineAgents[0]?.a_id as string) || ''

  const [platformOverride, setPlatformOverride] = useState<'linux' | 'windows' | null>(null)
  const [rows, setRows] = useState<BofRow[]>([])
  const [cmdTree, setCmdTree] = useState<CmdBatch[]>([])
  const [serverBase, setServerBase] = useState('')
  const [offline, setOffline] = useState(false)
  const [loading, setLoading] = useState(false)
  const [listErr, setListErr] = useState('')
  const [base, setBase] = useState(() => { try { return localStorage.getItem(BASE_KEY) || '' } catch { return '' } })
  const [baseOpen, setBaseOpen] = useState(false)
  const [search, setSearch] = useState('')
  const [catFilter, setCatFilter] = useState<Cat | 'all'>('all')
  const [statusFilter, setStatusFilter] = useState<StatusFilter>('all')
  const [selectedName, setSelectedName] = useState('')
  const [recentNames, setRecentNames] = useState<string[]>(() => loadStringList(RECENT_KEY, 6))
  const [favNames, setFavNames] = useState<string[]>(() => loadStringList(FAV_KEY, 50))
  const [collapsedCats, setCollapsedCats] = useState<Set<string>>(() => new Set())

  // Windows / Extension-Kit state
  const [extPurpose, setExtPurpose] = useState<string>('all')
  const [extSuite, setExtSuite] = useState<string>('all')
  const [extRecentOnly, setExtRecentOnly] = useState(false)
  const [extFavOnly, setExtFavOnly] = useState(false)
  const [extSelectedKey, setExtSelectedKey] = useState('')
  const [extSubByKey, setExtSubByKey] = useState<Record<string, string>>({})
  const [expandedExtKeys, setExpandedExtKeys] = useState<Set<string>>(() => new Set())
  const [subFilter, setSubFilter] = useState('')
  const [customBofOpen, setCustomBofOpen] = useState(false)
  const [customBofPath, setCustomBofPath] = useState('')
  const [customBofArgs, setCustomBofArgs] = useState('')
  const [customBofAsync, setCustomBofAsync] = useState(false)
  const [winExecEntries, setWinExecEntries] = useState<Array<{ name: string; label?: string; path: string; size?: number; stale?: boolean }>>([])
  const [winExecBase, setWinExecBase] = useState('')
  const [winExecNote, setWinExecNote] = useState('')
  const [extArgVals, setExtArgVals] = useState<Record<string, string>>({})
  const [extCustomCmd, setExtCustomCmd] = useState<Record<string, string>>({})
  const [recentExtKeys, setRecentExtKeys] = useState<string[]>(() => loadStringList(RECENT_EXT_KEY, 6))
  const [favExtKeys, setFavExtKeys] = useState<string[]>(() => loadStringList(FAV_EXT_KEY, 50))
  const [extCtx, setExtCtx] = useState<{ x: number; y: number; item: ExtBofItem } | null>(null)

  const [paramVals, setParamVals] = useState<Record<string, string>>({})
  const [missingKey, setMissingKey] = useState<string | null>(null)
  const [asyncOn, setAsyncOn] = useState<Record<string, boolean>>({})
  const [busy, setBusy] = useState<string | null>(null)
  const [uploading, setUploading] = useState(false)
  const [compiling, setCompiling] = useState<string | null>(null)
  const [compileAll, setCompileAll] = useState(false)
  const [dragOver, setDragOver] = useState(false)
  const [out, setOut] = useState<{ title: string; text: string; bad: boolean } | null>(null)
  const [ctx, setCtx] = useState<{ x: number; y: number; row: BofRow } | null>(null)
  const [splitPct, setSplitPct] = useState(() => {
    try {
      const n = Number(localStorage.getItem(SPLIT_KEY))
      return Number.isFinite(n) && n >= 26 && n <= 64 ? n : 44
    } catch { return 44 }
  })
  const [draggingSplit, setDraggingSplit] = useState(false)

  const fileRef = useRef<HTMLInputElement | null>(null)
  const searchRef = useRef<HTMLInputElement | null>(null)
  const splitRef = useRef<HTMLDivElement | null>(null)
  const paramRefs = useRef<Record<string, HTMLInputElement | null>>({})

  const effectiveBase = base.trim() || serverBase
  const recentSet = useMemo(() => new Set(recentNames), [recentNames])
  const favSet = useMemo(() => new Set(favNames), [favNames])
  const recentExtSet = useMemo(() => new Set(recentExtKeys), [recentExtKeys])
  const favExtSet = useMemo(() => new Set(favExtKeys), [favExtKeys])

  const selectedAgent = effectiveAgent ? agents[effectiveAgent] : null
  const targetPlatform: PlatformKind = selectedAgent ? detectPlatform(selectedAgent) : (platformOverride || 'windows')
  const activePlatform: PlatformKind = platformOverride ?? targetPlatform
  const unsupportedReason = bofUnsupportedReason(selectedAgent)

  const load = useCallback(async (forceTree = false) => {
    if (!api) return
    setLoading(true)
    setListErr('')
    const [bofRes, treeRes] = await Promise.allSettled([
      api.get<any>('/api/bofs/list'),
      loadCmdTree(api, forceTree),
    ])
    if (bofRes.status === 'fulfilled') {
      const d = bofRes.value
      const entries: BofEntry[] = Array.isArray(d?.entries) ? d.entries : []
      setRows(entries.map((e) => {
        const meta = META[e.name]
        return { ...e, meta: meta || UNKNOWN_META, file: `${e.name}.o`, known: !!meta }
      }))
      if (typeof d?.base === 'string' && d.base) setServerBase(d.base)
      setOffline(false)
    } else {
      setOffline(true)
      setRows(OFFLINE_ROWS)
      setListErr(String(bofRes.reason?.message || bofRes.reason || ''))
    }
    if (treeRes.status === 'fulfilled') {
      setCmdTree(Array.isArray(treeRes.value) ? treeRes.value : [])
    }
    setLoading(false)
  }, [api])

  useEffect(() => { void load(false) }, [load])

  // 拉取 Windows COFF .o 候选清单与服务端沙箱根目录（供「自定义 .o」自动补全及 FILE 参数提示）
  useEffect(() => {
    if (!api || activePlatform === 'linux') return
    let cancelled = false
    const reqAgent = selectedAgent && detectPlatform(selectedAgent) === 'windows' ? (selectedAgent.a_name || 'beacon') : 'beacon'
    const reqArch = selectedAgent?.a_arch || 'x64'
    api.post<any>('/api/bofs/exec', { agent: reqAgent, os: 1, arch: reqArch })
      .then((res) => {
        if (cancelled || !res) return
        setWinExecEntries(Array.isArray(res.entries) ? res.entries : [])
        setWinExecBase(typeof res.base === 'string' ? res.base : '')
        setWinExecNote(typeof res.note === 'string' ? res.note : '')
      })
      .catch(() => { /* optional endpoint */ })
    return () => { cancelled = true }
  }, [api, activePlatform, selectedAgent?.a_name, selectedAgent?.a_os, selectedAgent?.a_arch])

  // Close row context menu on outside click / scroll
  useEffect(() => {
    if (!ctx && !extCtx) return
    const close = () => { setCtx(null); setExtCtx(null) }
    window.addEventListener('click', close)
    window.addEventListener('scroll', close, true)
    return () => {
      window.removeEventListener('click', close)
      window.removeEventListener('scroll', close, true)
    }
  }, [ctx, extCtx])

  // Vertical splitter drag between left list pane and right detail inspector
  useEffect(() => {
    if (!draggingSplit) return
    const onMove = (e: MouseEvent) => {
      const box = splitRef.current?.getBoundingClientRect()
      if (!box || box.width <= 0) return
      const pct = ((e.clientX - box.left) / box.width) * 100
      const clamped = Math.max(26, Math.min(64, Math.round(pct)))
      setSplitPct(clamped)
      try { localStorage.setItem(SPLIT_KEY, String(clamped)) } catch { /* ignore */ }
    }
    const onUp = () => setDraggingSplit(false)
    window.addEventListener('mousemove', onMove)
    window.addEventListener('mouseup', onUp)
    return () => {
      window.removeEventListener('mousemove', onMove)
      window.removeEventListener('mouseup', onUp)
    }
  }, [draggingSplit])

  function invalidateCompletions() {
    clearCompletionCache()
  }

  function recordRecent(name: string) {
    setRecentNames((prev) => {
      const next = [name, ...prev.filter((x) => x !== name)].slice(0, 6)
      try { localStorage.setItem(RECENT_KEY, JSON.stringify(next)) } catch { /* ignore */ }
      return next
    })
  }

  function saveBase() {
    try { localStorage.setItem(BASE_KEY, base.trim()) } catch { /* ignore */ }
    pushToast('ok', base.trim() ? 'BOF 基路径已保存' : '已改为跟随服务端')
  }

  const fullPath = useCallback((b: BofRow) => {
    const bstr = effectiveBase.trim()
    if (!bstr) return b.file
    return (bstr.endsWith('/') ? bstr : bstr + '/') + b.file
  }, [effectiveBase])

  const val = (b: BofRow, key: string) => (paramVals[`${b.name}.${key}`] || '').trim()

  function collectArgs(b: BofRow): BofArg[] {
    const outArgs: BofArg[] = []
    for (const p of b.meta.params || []) {
      if (p.flag) {
        if (paramVals[`${b.name}.${p.key}`]) outArgs.push({ kind: 'str', value: p.placeholder })
        continue
      }
      const v = val(b, p.key)
      if (!v) continue
      if (p.multi) {
        for (const tok of v.split(/\s+/).filter(Boolean)) outArgs.push({ kind: 'str', value: tok })
      } else if (p.nulJoin) {
        outArgs.push({ kind: 'str', value: v.split(/\s+/).filter(Boolean).join('\0') })
      } else {
        outArgs.push({ kind: p.kind === 'int' ? 'int' : 'str', value: v })
      }
    }
    return outArgs
  }

  function asyncMark() {
    return isLinuxAgent(agents[effectiveAgent] as any) ? 'async' : '-a'
  }

  function buildCmdline(b: BofRow): string {
    const parts = [`execute bof ${fullPath(b)}`]
    for (const a of collectArgs(b)) {
      if (a.kind === 'int') { parts.push(a.value); continue }
      if (/[\s\0]/.test(a.value)) parts.push(`"${a.value.replace(/\0/g, ' ')}"`)
      else parts.push(a.value)
    }
    if (asyncOn[b.name]) parts.push(asyncMark())
    return parts.join(' ')
  }

  async function runBof(b: BofRow) {
    const id = effectiveAgent
    if (!id) { pushToast('warn', '没有在线主机可选'); return }
    if (!api) { pushToast('err', t('API 未就绪')); return }
    if (unsupportedReason) { pushToast('warn', unsupportedReason); return }
    if (targetPlatformMismatch) { pushToast('warn', '当前所选主机不是 Linux，请先切到对应主机'); return }
    if (!b.obj && !offline) { pushToast('warn', `${b.name}.o 在服务端不存在，先编译或上传`); return }
    for (const p of b.meta.params || []) {
      if (p.required && !p.flag && !val(b, p.key)) {
        const k = `${b.name}.${p.key}`
        setSelectedName(b.name)
        setMissingKey(k)
        setTimeout(() => paramRefs.current[k]?.focus(), 0)
        pushToast('warn', `缺少必填参数：${p.label}`)
        return
      }
    }

    const cmdline = buildCmdline(b)
    const args = collectArgs(b)
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
        await sendCommand(cmdline)
      }
      recordRecent(b.name)
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
    pushToast(ok ? 'ok' : 'err', ok ? '命令已复制' : t('复制失败'))
  }

  async function uploadFile(f: File) {
    if (!api) return
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
        setOut({ title: `${name}.c 已上传，但编译失败`, text: String(res?.compile?.output || res?.compile?.error || ''), bad: true })
        pushToast('warn', `${name}.c 已上传，但编译失败（见下方输出）`)
      } else {
        pushToast('ok', kind === 'c' ? `${name}.c 已上传并编译` : `${name}.o 已上传`)
      }
      setSelectedName(name)
      await load()
      invalidateCompletions()
    } catch (e: any) {
      pushToast('err', String(e?.message || e))
    } finally {
      setUploading(false)
    }
  }

  async function onPickFile(ev: React.ChangeEvent<HTMLInputElement>) {
    const f = ev.target.files?.[0]
    ev.target.value = ''
    if (f) await uploadFile(f)
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

  // Counts & filtering
  const catCounts = useMemo(() => {
    const m: Record<Cat, number> = { recon: 0, priv: 0, cred: 0, persist: 0, stealth: 0, exec: 0, other: 0 }
    for (const r of rows) m[r.meta.category] = (m[r.meta.category] || 0) + 1
    return m
  }, [rows])

  const nReady = useMemo(() => rows.filter((r) => r.obj && !r.stale).length, [rows])
  const nObj = useMemo(() => rows.filter((r) => r.obj).length, [rows])
  const nStale = useMemo(() => rows.filter((r) => r.stale).length, [rows])
  const nUncompiled = useMemo(() => rows.filter((r) => !offline && !r.obj).length, [rows, offline])
  const nOther = useMemo(() => rows.filter((r) => !r.known).length, [rows])
  const nRecent = useMemo(() => rows.filter((r) => recentSet.has(r.name)).length, [rows, recentSet])
  const nFav = useMemo(() => rows.filter((r) => favSet.has(r.name)).length, [rows, favSet])

  const filtered = useMemo(() => {
    const q = search.trim().toLowerCase()
    return rows.filter((b) => {
      if (catFilter !== 'all' && b.meta.category !== catFilter) return false
      if (statusFilter === 'ready' && !b.obj) return false
      if (statusFilter === 'stale' && !b.stale) return false
      if (statusFilter === 'uncompiled' && (offline || b.obj)) return false
      if (statusFilter === 'unregistered' && b.known) return false
      if (statusFilter === 'recent' && !recentSet.has(b.name)) return false
      if (statusFilter === 'fav' && !favSet.has(b.name)) return false
      if (!q) return true
      return [b.name, b.file, b.meta.desc, ...(b.meta.notes || [])]
        .some((s) => s.toLowerCase().includes(q))
    })
  }, [rows, search, catFilter, statusFilter, offline, recentSet, favSet])

  const grouped = useMemo(() => {
    const byCat = new Map<Cat, BofRow[]>()
    for (const b of filtered) {
      const list = byCat.get(b.meta.category) || []
      list.push(b)
      byCat.set(b.meta.category, list)
    }
    return CATS
      .map((c) => ({ cat: c.key, label: c.label, items: byCat.get(c.key) || [] }))
      .filter((g) => g.items.length > 0)
  }, [filtered])

  const flatOrder = useMemo(() => grouped.flatMap((g) => g.items), [grouped])

  // Keep valid active selection whenever rows or filter list changes
  const selected = useMemo(
    () => flatOrder.find((r) => r.name === selectedName) || flatOrder[0] || null,
    [flatOrder, selectedName],
  )

  useEffect(() => {
    if (selected && selected.name !== selectedName) setSelectedName(selected.name)
  }, [selected, selectedName])

  const onSelectRow = useCallback((name: string) => {
    setSelectedName(name)
    setMissingKey(null)
  }, [])

  const onSelectExtRow = useCallback((key: string) => {
    setExtSelectedKey(key)
    setSubFilter('')
    setMissingKey(null)
  }, [])

  const onSelectExtSub = useCallback((key: string, subName: string) => {
    setExtSelectedKey(key)
    setExtSubByKey((p) => ({ ...p, [key]: subName }))
    setExpandedExtKeys((prev) => {
      const n = new Set(prev)
      n.add(key)
      return n
    })
    setMissingKey(null)
  }, [])

  const onToggleCollapse = useCallback((groupKey: string) => {
    setCollapsedCats((prev) => {
      const next = new Set(prev)
      if (next.has(groupKey)) next.delete(groupKey)
      else next.add(groupKey)
      return next
    })
  }, [])

  const onToggleExpandExt = useCallback((key: string) => {
    setExpandedExtKeys((prev) => {
      const next = new Set(prev)
      if (next.has(key)) next.delete(key)
      else next.add(key)
      return next
    })
  }, [])

  const onToggleFavLinux = useCallback((name: string) => {
    setFavNames((prev) => {
      const next = prev.includes(name) ? prev.filter((x) => x !== name) : [name, ...prev].slice(0, 50)
      try { localStorage.setItem(FAV_KEY, JSON.stringify(next)) } catch { /* ignore */ }
      return next
    })
  }, [])

  const onToggleFavExt = useCallback((key: string) => {
    setFavExtKeys((prev) => {
      const next = prev.includes(key) ? prev.filter((x) => x !== key) : [key, ...prev].slice(0, 50)
      try { localStorage.setItem(FAV_EXT_KEY, JSON.stringify(next)) } catch { /* ignore */ }
      return next
    })
  }, [])

  const onRowContext = useCallback((x: number, y: number, row: BofRow) => {
    setCtx({ x, y, row })
  }, [])

  const onExtRowContext = useCallback((x: number, y: number, item: ExtBofItem) => {
    setExtCtx({ x, y, item })
  }, [])

  const onResetFilters = useCallback(() => {
    setSearch('')
    setCatFilter('all')
    setStatusFilter('all')
    setExtPurpose('all')
    setExtSuite('all')
    setExtRecentOnly(false)
    setExtFavOnly(false)
    setSubFilter('')
  }, [])

  const onQuickRun = useCallback((row: BofRow) => {
    void runBof(row)
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [effectiveAgent, api, offline, effectiveBase, asyncOn])

  function onSelectAgent(nextId: string) {
    setAgentId(nextId)
    setPlatformOverride(null)
    setMissingKey(null)
    setOut(null)
  }

  function switchPlatform(nextPlat: 'linux' | 'windows') {
    setPlatformOverride(nextPlat)
    setMissingKey(null)
    setOut(null)
    if (selectedAgent && detectPlatform(selectedAgent) === nextPlat) return
    const match = onlineAgents.find((a: any) => detectPlatform(a) === nextPlat)
    if (match?.a_id) setAgentId(match.a_id)
  }

  function recordRecentExt(key: string) {
    setRecentExtKeys((prev) => {
      const next = [key, ...prev.filter((x) => x !== key)].slice(0, 6)
      try { localStorage.setItem(RECENT_EXT_KEY, JSON.stringify(next)) } catch { /* ignore */ }
      return next
    })
  }

  // ── Windows / macOS Extension-Kit 动态 BOF 目录解析（source=1 / source=2，按 agent + os 去重） ──
  const extCatalog = useMemo(() => {
    if (activePlatform === 'linux') return { items: [] as ExtBofItem[], agentMismatch: false, batchAgent: '' }
    const osCode = activePlatform === 'macos' ? 3 : 1
    const allExtBatches = cmdTree.filter((b) => (b.source === 1 || b.source === 2) && (b.groups || []).some((g) => (g.commands || []).length > 0))
    const byOs = allExtBatches.filter((b) => b.os === osCode || !b.os)

    let chosen: CmdBatch[] = []
    let agentMismatch = false
    const targetAgentMatched = selectedAgent && detectPlatform(selectedAgent) === activePlatform ? selectedAgent : null
    if (targetAgentMatched?.a_name) {
      chosen = byOs.filter((b) => b.agent === targetAgentMatched.a_name)
      if (!chosen.length && byOs.length) {
        agentMismatch = true
        chosen = byOs
      }
    } else {
      const preferredAgent = byOs.find((b) => b.agent === 'beacon')?.agent || byOs[0]?.agent || ''
      chosen = preferredAgent ? byOs.filter((b) => b.agent === preferredAgent) : byOs
    }

    const seen = new Set<string>()
    const items: ExtBofItem[] = []
    for (const batch of chosen) {
      for (const grp of batch.groups || []) {
        const suite = grp.group_name || grp.script_name || 'Extension-Kit'
        const purpose = purposeOf(suite)
        for (const cmd of grp.commands || []) {
          const key = `${suite}::${cmd.name}`
          if (seen.has(key)) continue
          seen.add(key)
          items.push({
            key,
            suite,
            scriptName: grp.script_name || '',
            purpose,
            batchAgent: batch.agent || '',
            cmd,
          })
        }
      }
    }
    return {
      items,
      agentMismatch,
      batchAgent: items[0]?.batchAgent || chosen[0]?.agent || '',
    }
  }, [activePlatform, cmdTree, selectedAgent])

  const extPurposeCounts = useMemo(() => {
    const m = new Map<string, number>()
    for (const it of extCatalog.items) m.set(it.purpose, (m.get(it.purpose) || 0) + 1)
    return m
  }, [extCatalog.items])

  const extSuites = useMemo(() => {
    const s = new Set<string>()
    for (const it of extCatalog.items) s.add(it.suite)
    return Array.from(s)
  }, [extCatalog.items])

  const nExtRecent = useMemo(
    () => extCatalog.items.filter((it) => recentExtSet.has(it.key)).length,
    [extCatalog.items, recentExtSet],
  )
  const nExtFav = useMemo(
    () => extCatalog.items.filter((it) => favExtSet.has(it.key)).length,
    [extCatalog.items, favExtSet],
  )
  const extLeafCountTotal = useMemo(
    () => extCatalog.items.reduce((n, it) => n + Math.max(1, (it.cmd.subcommands || []).length), 0),
    [extCatalog.items],
  )

  const extFiltered = useMemo(() => {
    const q = search.trim().toLowerCase()
    return extCatalog.items.filter((it) => {
      if (extPurpose !== 'all' && it.purpose !== extPurpose) return false
      if (extSuite !== 'all' && it.suite !== extSuite) return false
      if (extRecentOnly && !recentExtSet.has(it.key)) return false
      if (extFavOnly && !favExtSet.has(it.key)) return false
      if (!q) return true
      const c = it.cmd
      const hay = [
        c.name, c.description, c.message, c.example, it.suite, it.purpose,
        ...(c.args || []).flatMap((a) => [a.name, a.mark, a.description]),
        ...(c.subcommands || []).flatMap((s) => [s.name, s.description, s.example]),
      ]
      return hay.some((v) => (v || '').toLowerCase().includes(q))
    })
  }, [extCatalog.items, extPurpose, extSuite, extRecentOnly, extFavOnly, recentExtSet, favExtSet, search])

  const extGrouped = useMemo(() => {
    const byPurpose = new Map<string, ExtBofItem[]>()
    for (const it of extFiltered) {
      const list = byPurpose.get(it.purpose) || []
      list.push(it)
      byPurpose.set(it.purpose, list)
    }
    return PURPOSE_ORDER
      .filter((p) => byPurpose.has(p))
      .map((p) => ({ purpose: p, items: byPurpose.get(p)! }))
  }, [extFiltered])

  const extFlatOrder = useMemo(() => extGrouped.flatMap((g) => g.items), [extGrouped])
  const selectedExt = useMemo(
    () => extFlatOrder.find((it) => it.key === extSelectedKey) || extFlatOrder[0] || null,
    [extFlatOrder, extSelectedKey],
  )

  useEffect(() => {
    if (activePlatform !== 'linux' && selectedExt && selectedExt.key !== extSelectedKey) {
      setExtSelectedKey(selectedExt.key)
    }
  }, [activePlatform, selectedExt, extSelectedKey])

  // 搜索命中某个子命令（例如 asktgt / get-users）时，自动选中对应子命令
  useEffect(() => {
    const q = search.trim().toLowerCase()
    if (!q || activePlatform === 'linux' || !selectedExt) return
    const subs = selectedExt.cmd.subcommands || []
    if (!subs.length) return
    const exactOrMatch = subs.find((s) => s.name.toLowerCase() === q)
      || subs.find((s) => `${s.name} ${s.description || ''}`.toLowerCase().includes(q))
    if (exactOrMatch && extSubByKey[selectedExt.key] !== exactOrMatch.name) {
      setExtSubByKey((p) => ({ ...p, [selectedExt.key]: exactOrMatch.name }))
    }
  }, [search, activePlatform, selectedExt, extSubByKey])

  const selectedArgs = selected ? collectArgs(selected) : []
  const selectedCmdline = selected ? buildCmdline(selected) : ''
  const hasAnyParamValue = !!(selected && (selected.meta.params || []).some((p) => !!paramVals[`${selected.name}.${p.key}`]))

  const extSubs = selectedExt?.cmd.subcommands || []
  const activeExtSub = extSubs.length
    ? (extSubs.find((s) => s.name === extSubByKey[selectedExt!.key]) || extSubs[0])
    : null
  const activeExtCmd = activeExtSub || selectedExt?.cmd || null
  const extBaseArgKey = selectedExt ? `${selectedExt.key}:${activeExtSub ? activeExtSub.name : ''}` : ''
  const autoExtCmdline = selectedExt
    ? buildExtCmdline(selectedExt, activeExtSub?.name || '', extArgVals)
    : ''
  const finalExtCmdline = extBaseArgKey && extBaseArgKey in extCustomCmd
    ? extCustomCmd[extBaseArgKey]
    : autoExtCmdline
  const hasAnyExtArgVal = !!(selectedExt && activeExtCmd && (
    (extBaseArgKey in extCustomCmd) ||
    (activeExtCmd.args || []).some((_, idx) => !!extArgVals[`${extBaseArgKey}.${idx}`])
  ))
  const targetPlatformMismatch = !!(selectedAgent && detectPlatform(selectedAgent) !== activePlatform)
  const extAgentBatchMismatch = !!(
    activePlatform !== 'linux' &&
    selectedExt &&
    selectedAgent &&
    selectedExt.batchAgent &&
    selectedAgent.a_name !== selectedExt.batchAgent
  )

  async function runExtBof(it: ExtBofItem, customOverride?: string) {
    if (!effectiveAgent || !selectedAgent) { pushToast('warn', '没有在线主机可选'); return }
    if (!api) { pushToast('err', t('API 未就绪')); return }
    if (unsupportedReason) { pushToast('warn', unsupportedReason); return }
    if (detectPlatform(selectedAgent) !== activePlatform) {
      pushToast('warn', `当前目标主机是 ${platformLabel(detectPlatform(selectedAgent))}，请先切换匹配的在线主机`)
      return
    }
    if (it.batchAgent && selectedAgent.a_name !== it.batchAgent) {
      pushToast('warn', `当前插件属于 ${it.batchAgent}，与所选主机 ${selectedAgent.a_name} 不匹配`)
      return
    }

    const subs = it.cmd.subcommands || []
    const sub = subs.length ? (subs.find((s) => s.name === extSubByKey[it.key]) || subs[0]) : null
    const act = sub || it.cmd
    const baseKey = `${it.key}:${sub ? sub.name : ''}`
    const cmdText = (customOverride ?? (baseKey in extCustomCmd ? extCustomCmd[baseKey] : buildExtCmdline(it, sub?.name || '', extArgVals))).trim()

    if (!(baseKey in extCustomCmd) && customOverride === undefined) {
      const argsList = act.args || []
      for (let i = 0; i < argsList.length; i++) {
        const a = argsList[i]
        if (!a.required || (a.flag && !a.name)) continue
        if (!extArgDisplayValue(extArgVals, baseKey, a, i).trim()) {
          const k = `ext:${baseKey}.${i}`
          setExtSelectedKey(it.key)
          setMissingKey(k)
          setTimeout(() => paramRefs.current[k]?.focus(), 0)
          pushToast('warn', `缺少必填参数：${a.name || a.description || '参数'}`)
          return
        }
      }
    }
    if (/<[^>]+>|\{[^}]+\}/.test(cmdText)) {
      pushToast('warn', '命令行中仍存在未替换的占位符（<...> / {...}），请填写参数后执行')
      return
    }

    setBusy(it.key)
    try {
      await api.commandRaw(effectiveAgent, cmdText)
      recordRecentExt(it.key)
      pushLog('info', `${platformLabel(activePlatform)} BOF ${it.cmd.name} 已下发 → ${cmdText}`, false)
      openConsole(effectiveAgent)
    } catch (e: any) {
      pushToast('err', `${it.cmd.name} 下发失败: ${e?.message || e}`)
    } finally {
      setBusy(null)
    }
  }

  function clearSelectedParams() {
    if (activePlatform === 'linux') {
      if (!selected) return
      setParamVals((prev) => {
        const next = { ...prev }
        for (const p of selected.meta.params || []) delete next[`${selected.name}.${p.key}`]
        return next
      })
      setMissingKey(null)
      return
    }
    if (!selectedExt || !activeExtCmd) return
    setExtArgVals((prev) => {
      const next = { ...prev }
      for (let i = 0; i < (activeExtCmd.args || []).length; i++) delete next[`${extBaseArgKey}.${i}`]
      return next
    })
    setExtCustomCmd((prev) => {
      const next = { ...prev }
      delete next[extBaseArgKey]
      return next
    })
    setMissingKey(null)
  }

  function onPanelKeyDown(e: React.KeyboardEvent<HTMLDivElement>) {
    if ((e.ctrlKey || e.metaKey) && (e.key === 'f' || e.key === 'F')) {
      e.preventDefault()
      searchRef.current?.focus()
      searchRef.current?.select()
      return
    }
    if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') {
      if (activePlatform === 'linux' && selected) {
        e.preventDefault()
        void runBof(selected)
      } else if (activePlatform !== 'linux' && selectedExt) {
        e.preventDefault()
        void runExtBof(selectedExt)
      }
      return
    }
    if (e.key === 'Escape') {
      if (ctx) { setCtx(null); return }
      if (extCtx) { setExtCtx(null); return }
      if (baseOpen) { setBaseOpen(false); return }
      if (out) { setOut(null); return }
    }
    const tag = (e.target as HTMLElement | null)?.tagName
    const inField = tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT'
    if ((e.key === 'ArrowDown' || e.key === 'ArrowUp') && (!inField || e.altKey)) {
      if (activePlatform === 'linux' && flatOrder.length > 0) {
        e.preventDefault()
        const idx = selected ? flatOrder.findIndex((x) => x.name === selected.name) : -1
        const nextIdx = e.key === 'ArrowDown'
          ? Math.min(flatOrder.length - 1, idx + 1)
          : Math.max(0, (idx < 0 ? 0 : idx - 1))
        const target = flatOrder[nextIdx]
        if (target) {
          onSelectRow(target.name)
          const btn = splitRef.current?.querySelector(`[data-bof-name="${CSS.escape(target.name)}"]`) as HTMLElement | null
          btn?.scrollIntoView({ block: 'nearest' })
        }
      } else if (activePlatform !== 'linux' && extFlatOrder.length > 0) {
        e.preventDefault()
        const idx = selectedExt ? extFlatOrder.findIndex((x) => x.key === selectedExt.key) : -1
        const nextIdx = e.key === 'ArrowDown'
          ? Math.min(extFlatOrder.length - 1, idx + 1)
          : Math.max(0, (idx < 0 ? 0 : idx - 1))
        const target = extFlatOrder[nextIdx]
        if (target) {
          onSelectExtRow(target.key)
          const btn = splitRef.current?.querySelector(`[data-bof-name="${CSS.escape(target.key)}"]`) as HTMLElement | null
          btn?.scrollIntoView({ block: 'nearest' })
        }
      }
    }
  }

  return (
    <div
      className="qt-bof"
      tabIndex={-1}
      onKeyDown={onPanelKeyDown}
      onDragOver={(e) => {
        if (activePlatform === 'linux' && e.dataTransfer?.types?.includes('Files')) {
          e.preventDefault()
          if (!dragOver) setDragOver(true)
        }
      }}
      onDragLeave={(e) => {
        if (!e.currentTarget.contains(e.relatedTarget as Node | null)) setDragOver(false)
      }}
      onDrop={(e) => {
        if (activePlatform !== 'linux') return
        e.preventDefault()
        setDragOver(false)
        const f = e.dataTransfer?.files?.[0]
        if (f) void uploadFile(f)
      }}
    >
      {/* ── 1. Top Command Bar ── */}
      <div className="qt-bof-bar">
        <div className="qt-bof-title">
          <img className="qt-ico qt-ico-mono" src={qtAsset('icons/code_blocks_64dp.png')} alt="" />
          <span>{t('BOF 模块')}</span>
        </div>

        <div className="qt-bof-target" title={t('目标主机（仅在线）；切换主机自动匹配对应平台 BOF 插件')}>
          <span className={`qt-bof-target-dot ${effectiveAgent ? 'on' : ''}`} />
          <select value={effectiveAgent} onChange={(e) => onSelectAgent(e.target.value)}>
            {onlineAgents.length === 0 && <option value="">{t('无在线主机')}</option>}
            {onlineAgents.map((a: any) => (
              <option key={a.a_id} value={a.a_id}>
                {a.a_computer || a.a_id} ({a.a_username || '-'} · {platformLabel(detectPlatform(a))})
              </option>
            ))}
          </select>
        </div>

        <div className="qt-bof-plat-seg" title="按目标平台切换 BOF 插件库（选主机时默认自动跟随）">
          <button
            type="button"
            className={`qt-bof-plat-btn ${activePlatform === 'linux' ? 'active' : ''}`}
            onClick={() => switchPlatform('linux')}
          >
            Linux BOF
          </button>
          <button
            type="button"
            className={`qt-bof-plat-btn ${activePlatform === 'windows' ? 'active' : ''}`}
            onClick={() => switchPlatform('windows')}
          >
            Windows BOF
          </button>
          {activePlatform === 'macos' && (
            <span className="qt-bof-plat-btn active" style={{ cursor: 'default' }}>macOS</span>
          )}
        </div>

        <div className="qt-bof-search-wrap">
          <input
            ref={searchRef}
            className="qt-bof-search"
            value={search}
            onChange={(e) => setSearch(e.target.value)}
            placeholder={activePlatform === 'linux' ? t('搜索 Linux BOF、用途或注意事项… (Ctrl+F)') : t('搜索 Windows 插件、命令或示例… (Ctrl+F)')}
          />
          {search && (
            <button
              type="button"
              className="qt-bof-search-clear"
              title="清空搜索"
              onClick={() => setSearch('')}
            >×</button>
          )}
        </div>

        <span style={{ flex: 1 }} />

        {activePlatform === 'linux' && (
          <>
            <input
              ref={fileRef}
              type="file"
              accept=".c,.o"
              style={{ display: 'none' }}
              onChange={(e) => void onPickFile(e)}
            />
            <button
              type="button"
              className="qt-bof-btn"
              disabled={uploading || !api}
              onClick={() => fileRef.current?.click()}
              title={t('上传 .c（自动编译）或 .o（直接投递），也支持拖拽到界面')}
            >
              <img className="qt-ico qt-ico-mono" src={qtAsset('icons/upload_file_64dp.png')} alt="" />
              <span>{uploading ? '上传中…' : t('上传 BOF')}</span>
            </button>

            <button
              type="button"
              className="qt-bof-btn"
              disabled={compileAll || !api}
              onClick={() => void compileEvery()}
              title={t('把服务端 src_agent/bofs/*.c 全部重编一遍')}
            >
              <img className="qt-ico qt-ico-mono" src={qtAsset('icons/restart_64dp.png')} alt="" />
              <span>{compileAll ? '重编中…' : '全部重编'}</span>
            </button>

            <button
              type="button"
              className={`qt-bof-btn ${baseOpen || base.trim() ? 'active' : ''}`}
              onClick={() => setBaseOpen((v) => !v)}
              title={t('配置服务端 BOF 基路径')}
            >
              <span>基路径</span>
            </button>
          </>
        )}

        {activePlatform !== 'linux' && (
          <button
            type="button"
            className={`qt-bof-btn ${customBofOpen ? 'active' : ''}`}
            onClick={() => setCustomBofOpen((v) => !v)}
            title="直接按服务端路径运行自定义 COFF .o（无需经过 Extension-Kit .axs）"
          >
            <span>自定义 .o</span>
          </button>
        )}

        <button
          type="button"
          className="qt-bof-btn"
          onClick={() => { void load(true); invalidateCompletions() }}
          disabled={loading || !api}
          title={t('重新拉取服务端 BOF 清单与 AxScript 命令树')}
        >
          <img className="qt-ico qt-ico-mono" src={qtAsset('icons/autorenew_64dp.png')} alt="" />
          <span>{loading ? '…' : t('刷新')}</span>
        </button>

        <PageTips id="bofs">
          <p><b>双平台自动分流：</b>{t('选中')} <b>Linux</b> {t('主机时展示 Linux ELF BOF（')}<code>/api/bofs/list</code>{t('）；选中')} <b>Windows</b> {t('主机时自动展示 Windows Extension-Kit BOF（')}<code>POST /api/axscript/commands</code> {t('中的')} <code>SAL-BOF / AD-BOF / Creds-BOF</code> {t('等全套套件）。')}</p>
          <p><b>Linux ELF BOF：</b>{t('支持上传')} <code>.c</code> {t('（服务端自动 gcc 编译）或')} <code>.o</code>{t('，支持按')} <code>BeaconDataInt</code> {t('与长字串精准打包。')}</p>
          <p><b>Windows BOF：</b>{t('命令经服务端 AxScript 预钩子自动展开为')} <code>execute bof …_bin/&lt;name&gt;.&lt;arch&gt;.o</code>{t('；多子命令套件（如 kerbeus/ldap/mssql）支持左侧树展开与右侧筛选；也可用「自定义 .o」直跑任意 COFF 文件。')}</p>
          <p>{t('快捷键：')}<code>Ctrl+F</code> {t('搜索 · ')}<code>↑/↓</code> {t('切换模块 · ')}<code>Ctrl+Enter</code> {t('立即执行 · 点击 ☆ 收藏常用插件。')}</p>
        </PageTips>
      </div>

      {/* ── Collapsible Base Path Config Strip (Linux only) ── */}
      {activePlatform === 'linux' && baseOpen && (
        <div className="qt-bof-base-bar">
          <label>服务端 BOF 目录</label>
          <input
            className="qt-bof-base-input"
            value={base}
            onChange={(e) => setBase(e.target.value)}
            onKeyDown={(e) => { if (e.key === 'Enter') saveBase() }}
            placeholder={serverBase || '/root/cc/run/extenders/linux_agent/bofs/'}
          />
          <button type="button" className="qt-bof-btn primary" onClick={saveBase}>保存</button>
          {base.trim() && (
            <button
              type="button"
              className="qt-bof-btn"
              onClick={() => {
                setBase('')
                try { localStorage.removeItem(BASE_KEY) } catch { /* ignore */ }
                pushToast('ok', '已改为跟随服务端')
              }}
            >
              跟随服务端
            </button>
          )}
          <button type="button" className="qt-bof-btn ghost" onClick={() => setBaseOpen(false)}>收起</button>
        </div>
      )}

      {/* ── Collapsible Standalone Custom COFF .o Strip (Windows/macOS) ── */}
      {activePlatform !== 'linux' && customBofOpen && (
        <div className="qt-bof-base-bar">
          <label title={winExecNote || '仅允许读取服务端 Extenders / AxScripts 沙箱目录内的 .o 文件'}>服务端 COFF .o 路径</label>
          <input
            className="qt-bof-base-input"
            list="qt-bof-win-o-list"
            value={customBofPath}
            onChange={(e) => setCustomBofPath(e.target.value)}
            placeholder={winExecBase ? `${winExecBase.replace(/\/+$/, '')}/custom.x64.o` : 'Extenders/Extension-Kit/.../custom.x64.o'}
          />
          {winExecEntries.length > 0 && (
            <datalist id="qt-bof-win-o-list">
              {winExecEntries.slice(0, 150).map((e) => (
                <option key={e.path} value={e.path}>{e.label || e.name}</option>
              ))}
            </datalist>
          )}
          <input
            className="qt-bof-base-input"
            style={{ flex: '0 1 200px', minWidth: 120 }}
            value={customBofArgs}
            onChange={(e) => setCustomBofArgs(e.target.value)}
            placeholder="参数（可选）"
          />
          <label className="qt-bof-async-chk" title="异步标志 -a（拼在 .o 路径之前，遵循 execute bof [-a] &lt;bof&gt; [param_data]）">
            <input type="checkbox" checked={customBofAsync} onChange={(e) => setCustomBofAsync(e.target.checked)} />
            <span>-a</span>
          </label>
          <span
            className="qt-bof-badge"
            style={{ flexShrink: 0 }}
            title={winExecNote || '服务端 FILE 参数沙箱仅允许读取 Extenders / AxScripts 目录内的文件'}
          >
            {winExecEntries.length > 0 ? `已扫描 ${winExecEntries.length} 个 .o` : '沙箱: Extenders/AxScripts'}
          </span>
          <button
            type="button"
            className="qt-bof-btn primary"
            disabled={!effectiveAgent || targetPlatformMismatch || !!unsupportedReason || !customBofPath.trim()}
            onClick={() => {
              if (unsupportedReason) { pushToast('warn', unsupportedReason); return }
              if (!api || !effectiveAgent || !customBofPath.trim()) return
              const p = customBofPath.trim()
              const qPath = /\s/.test(p) && !/^".*"$/.test(p) ? `"${p}"` : p
              const rawCmd = ['execute bof', customBofAsync ? '-a' : '', qPath, customBofArgs.trim()].filter(Boolean).join(' ')
              void api.commandRaw(effectiveAgent, rawCmd)
                .then(() => { pushLog('info', `自定义 COFF BOF 已下发 → ${rawCmd}`, false); openConsole(effectiveAgent) })
                .catch((err: any) => pushToast('err', err?.message || String(err)))
            }}
          >
            执行 .o
          </button>
          <button type="button" className="qt-bof-btn ghost" onClick={() => setCustomBofOpen(false)}>收起</button>
        </div>
      )}

      {/* ── 2. Category & Status Filter Strip ── */}
      {activePlatform === 'linux' ? (
        <div className="qt-bof-filter-bar">
          <div className="qt-bof-pills">
            <button
              type="button"
              className={`qt-bof-pill ${catFilter === 'all' ? 'active' : ''}`}
              onClick={() => setCatFilter('all')}
            >
              <span>全部</span>
              <span className="qt-bof-pill-cnt">{rows.length}</span>
            </button>
            {CATS.map((c) => {
              const cnt = catCounts[c.key] || 0
              if (cnt === 0) return null
              return (
                <button
                  key={c.key}
                  type="button"
                  className={`qt-bof-pill ${catFilter === c.key ? 'active' : ''}`}
                  onClick={() => setCatFilter(catFilter === c.key ? 'all' : c.key)}
                >
                  <span>{c.label}</span>
                  <span className="qt-bof-pill-cnt">{cnt}</span>
                </button>
              )
            })}
          </div>

          <div className="qt-bof-pills">
            <span className="qt-bof-filter-sep" />
            <button
              type="button"
              className={`qt-bof-pill ${statusFilter === 'all' ? 'active' : ''}`}
              onClick={() => setStatusFilter('all')}
            >
              <span>不限状态</span>
            </button>
            {!offline && (
              <button
                type="button"
                className={`qt-bof-pill ${statusFilter === 'ready' ? 'active' : ''}`}
                onClick={() => setStatusFilter(statusFilter === 'ready' ? 'all' : 'ready')}
              >
                <span>就绪</span>
                <span className="qt-bof-pill-cnt">{nReady}</span>
              </button>
            )}
            {nStale > 0 && (
              <button
                type="button"
                className={`qt-bof-pill ${statusFilter === 'stale' ? 'active' : ''}`}
                onClick={() => setStatusFilter(statusFilter === 'stale' ? 'all' : 'stale')}
              >
                <span>源码已改</span>
                <span className="qt-bof-pill-cnt">{nStale}</span>
              </button>
            )}
            {nUncompiled > 0 && (
              <button
                type="button"
                className={`qt-bof-pill ${statusFilter === 'uncompiled' ? 'active' : ''}`}
                onClick={() => setStatusFilter(statusFilter === 'uncompiled' ? 'all' : 'uncompiled')}
              >
                <span>未编译</span>
                <span className="qt-bof-pill-cnt">{nUncompiled}</span>
              </button>
            )}
            {nOther > 0 && (
              <button
                type="button"
                className={`qt-bof-pill ${statusFilter === 'unregistered' ? 'active' : ''}`}
                onClick={() => setStatusFilter(statusFilter === 'unregistered' ? 'all' : 'unregistered')}
              >
                <span>未登记</span>
                <span className="qt-bof-pill-cnt">{nOther}</span>
              </button>
            )}
            {nRecent > 0 && (
              <button
                type="button"
                className={`qt-bof-pill ${statusFilter === 'recent' ? 'active' : ''}`}
                onClick={() => setStatusFilter(statusFilter === 'recent' ? 'all' : 'recent')}
              >
                <span>最近</span>
                <span className="qt-bof-pill-cnt">{nRecent}</span>
              </button>
            )}
            {nFav > 0 && (
              <button
                type="button"
                className={`qt-bof-pill ${statusFilter === 'fav' ? 'active' : ''}`}
                onClick={() => setStatusFilter(statusFilter === 'fav' ? 'all' : 'fav')}
              >
                <span>★ 收藏</span>
                <span className="qt-bof-pill-cnt">{nFav}</span>
              </button>
            )}
          </div>
        </div>
      ) : (
        <div className="qt-bof-filter-bar">
          <div className="qt-bof-pills">
            <button
              type="button"
              className={`qt-bof-pill ${extPurpose === 'all' ? 'active' : ''}`}
              onClick={() => setExtPurpose('all')}
            >
              <span>全部</span>
              <span className="qt-bof-pill-cnt">{extCatalog.items.length}</span>
            </button>
            {PURPOSE_ORDER.map((p) => {
              const cnt = extPurposeCounts.get(p) || 0
              if (!cnt) return null
              return (
                <button
                  key={p}
                  type="button"
                  className={`qt-bof-pill ${extPurpose === p ? 'active' : ''}`}
                  onClick={() => setExtPurpose(extPurpose === p ? 'all' : p)}
                >
                  <span>{p}</span>
                  <span className="qt-bof-pill-cnt">{cnt}</span>
                </button>
              )
            })}
          </div>

          <div className="qt-bof-pills">
            <span className="qt-bof-filter-sep" />
            {extSuites.length > 0 && (
              <select
                className="qt-bof-suite-sel"
                value={extSuite}
                onChange={(e) => setExtSuite(e.target.value)}
                title="按 Extension-Kit 套件过滤"
              >
                <option value="all">全部套件 ({extSuites.length})</option>
                {extSuites.map((s) => (
                  <option key={s} value={s}>{s}</option>
                ))}
              </select>
            )}
            {nExtRecent > 0 && (
              <button
                type="button"
                className={`qt-bof-pill ${extRecentOnly ? 'active' : ''}`}
                onClick={() => setExtRecentOnly((v) => !v)}
              >
                <span>最近</span>
                <span className="qt-bof-pill-cnt">{nExtRecent}</span>
              </button>
            )}
            {nExtFav > 0 && (
              <button
                type="button"
                className={`qt-bof-pill ${extFavOnly ? 'active' : ''}`}
                onClick={() => setExtFavOnly((v) => !v)}
              >
                <span>★ 收藏</span>
                <span className="qt-bof-pill-cnt">{nExtFav}</span>
              </button>
            )}
          </div>
        </div>
      )}

      {activePlatform === 'linux' && offline && (
        <div className="qt-bof-offline-strip">
          <span>
            拿不到服务端 BOF 清单（{listErr || '未知原因'}）。当前为<b>{t('离线兜底列表')}</b>（未与服务端核对 .o 文件是否存在）。
          </span>
          <span style={{ flex: 1 }} />
          <button type="button" className="qt-bof-btn" onClick={() => void load(true)}>{t('重试')}</button>
        </div>
      )}

      {unsupportedReason && selectedAgent && (
        <div className="qt-bof-offline-strip">
          <span>
            <b>不支持执行 BOF：</b>{unsupportedReason}
          </span>
        </div>
      )}

      {targetPlatformMismatch && selectedAgent && (
        <div className="qt-bof-offline-strip">
          <span>
            当前展示 <b>{platformLabel(activePlatform)} BOF</b>，但选中主机 <b>{selectedAgent.a_computer || selectedAgent.a_id}</b> 属于 <b>{platformLabel(detectPlatform(selectedAgent))}</b>；执行已锁定，请切换到对应系统主机。
          </span>
          <span style={{ flex: 1 }} />
          {onlineAgents.some((a: any) => detectPlatform(a) === activePlatform) && (
            <button
              type="button"
              className="qt-bof-btn"
              onClick={() => {
                const m = onlineAgents.find((a: any) => detectPlatform(a) === activePlatform)
                if (m?.a_id) onSelectAgent(m.a_id)
              }}
            >
              切到 {platformLabel(activePlatform)} 主机
            </button>
          )}
        </div>
      )}

      {/* ── 3. Dual-Pane QSplitter Workspace (Master List + Detail Inspector) ── */}
      {activePlatform === 'linux' ? (
        <div className="qt-bof-split" ref={splitRef}>
          <BofListPane
            grouped={grouped}
            selectedName={selected?.name || ''}
            searchQuery={search}
            recentSet={recentSet}
            favSet={favSet}
            collapsedSet={collapsedCats}
            offline={offline}
            splitPct={splitPct}
            hasRows={rows.length > 0}
            onSelect={onSelectRow}
            onQuickRun={onQuickRun}
            onToggleCollapse={onToggleCollapse}
            onToggleFav={onToggleFavLinux}
            onContext={onRowContext}
            onResetFilters={onResetFilters}
          />

          <div
            className={`qt-vsplit ${draggingSplit ? 'dragging' : ''}`}
            role="separator"
            aria-orientation="vertical"
            title="拖拽调整左右栏宽度，双击还原"
            onMouseDown={() => setDraggingSplit(true)}
            onDoubleClick={() => {
              setSplitPct(44)
              try { localStorage.setItem(SPLIT_KEY, '44') } catch { /* ignore */ }
            }}
          />

          {/* Right Detail Inspector & Execution Dock (Linux ELF BOFs) */}
          <div className="qt-bof-detail-pane">
          {selected ? (
            <>
              <div className="qt-bof-detail-scroll">
                <div className="qt-bof-detail-hd">
                  <div className="qt-bof-detail-title-row">
                    <span className="qt-bof-detail-name">{selected.name}</span>
                    <span className="qt-bof-badge">{selected.file}</span>
                    <span className="qt-bof-badge">{CAT_LABEL[selected.meta.category]}</span>
                    {!selected.known && (
                      <span className="qt-bof-badge info" title={t('服务端有这个 .o，但本地 META 表里没有描述/参数表')}>{t('未登记')}</span>
                    )}
                    {!offline && !selected.obj && (
                      <span className="qt-bof-badge err" title={t('服务端 bofs/ 下没有这个 .o，无法执行')}>{t('未编译')}</span>
                    )}
                    {!offline && selected.obj && !selected.src && (
                      <span className="qt-bof-badge" title={t('服务端没有对应 .c，只能执行不能重编')}>{t('无源码')}</span>
                    )}
                    {!offline && selected.stale && (
                      <span className="qt-bof-badge warn" title={t('.c 比 .o 新，产物可能已过期')}>{t('源码已改')}</span>
                    )}
                    {!offline && selected.obj && !selected.stale && (
                      <span className="qt-bof-badge ok">就绪</span>
                    )}
                    <span style={{ flex: 1 }} />
                    <button
                      type="button"
                      className="qt-bof-btn"
                      disabled={compiling === selected.name || !api || (!selected.src && !offline)}
                      onClick={() => void compileOne(selected)}
                      title={selected.src || offline ? '用服务端源码重编这个 BOF' : '服务端没有 .c，无法重编'}
                    >
                      <img className="qt-ico qt-ico-mono" src={qtAsset('icons/restart_64dp.png')} alt="" />
                      <span>{compiling === selected.name ? '重编中…' : '重编'}</span>
                    </button>
                  </div>

                  <div className="qt-bof-detail-desc">{selected.meta.desc}</div>

                  {!offline && (
                    <div className="qt-bof-artifact-row">
                      <span>
                        .o {fmtSize(selected.obj_size)} · {fmtTime(selected.obj_mtime)}
                      </span>
                      {selected.obj_sha256 && (
                        <button
                          type="button"
                          className="qt-bof-sha"
                          title="点击复制完整 SHA256"
                          onClick={() => void copyText(selected.obj_sha256).then((ok) => pushToast(ok ? 'ok' : 'err', ok ? 'SHA256 已复制' : t('复制失败')))}
                        >
                          sha256:{selected.obj_sha256.slice(0, 12)}
                        </button>
                      )}
                      {selected.src && (
                        <span>.c {fmtSize(selected.src_size)} · {fmtTime(selected.src_mtime)}</span>
                      )}
                    </div>
                  )}
                </div>

                {/* Parameters Form Section */}
                <div className="qt-bof-sec">
                  <div className="qt-bof-sec-hd">
                    <span>
                      运行参数
                      {(selected.meta.params || []).length > 0
                        ? ` (${(selected.meta.params || []).length})`
                        : '（无参数）'}
                    </span>
                    {hasAnyParamValue && (
                      <button
                        type="button"
                        className="qt-bof-pill"
                        onClick={clearSelectedParams}
                      >
                        清空参数
                      </button>
                    )}
                  </div>

                  {(selected.meta.params || []).length === 0 ? (
                    <div style={{ fontSize: 11.5, color: 'var(--qt-text-muted)' }}>
                      {selected.known
                        ? '该模块无需任何参数，可直接下发执行。'
                        : '未在本地 META 登记参数表：本页面将只发送 .o 路径（若需传参，请按该 .c 的 Usage 在控制台手敲 execute bof）。'}
                    </div>
                  ) : (
                    <>
                      <div className="qt-bof-param-grid">
                        {(selected.meta.params || []).map((p) => {
                          const key = `${selected.name}.${p.key}`
                          if (p.flag) {
                            return (
                              <label key={p.key} className="qt-bof-flag-row" title={`勾上则原样发出 ${p.placeholder}`}>
                                <input
                                  type="checkbox"
                                  checked={!!paramVals[key]}
                                  onChange={(e) => setParamVals((s) => ({ ...s, [key]: e.target.checked ? '1' : '' }))}
                                />
                                <span>{p.label}</span>
                                <code>{p.placeholder}</code>
                              </label>
                            )
                          }
                          return (
                            <div key={p.key} style={{ display: 'contents' }}>
                              <label className="qt-bof-param-lbl" htmlFor={`bof-arg-${key}`}>
                                <span>
                                  {p.label}
                                  {p.required && <span className="qt-bof-req"> *</span>}
                                </span>
                                {p.multi ? (
                                  <span className="qt-bof-badge" title={t('空格分隔，每个值发成一个独立字段')}>{t('多个')}</span>
                                ) : p.nulJoin ? (
                                  <span className="qt-bof-badge info" title={t('空格分隔，合并成一个字段、值之间用 NUL 连接')}>NUL</span>
                                ) : p.kind === 'int' ? (
                                  <span className="qt-bof-badge warn" title={t('按裸 4 字节整数编码（BeaconDataInt），不是字符串')}>int32</span>
                                ) : null}
                              </label>
                              <input
                                id={`bof-arg-${key}`}
                                ref={(el) => { paramRefs.current[key] = el }}
                                className={`qt-bof-input ${missingKey === key ? 'invalid' : ''}`}
                                inputMode={p.kind === 'int' ? 'numeric' : undefined}
                                value={paramVals[key] || ''}
                                onChange={(e) => {
                                  if (missingKey === key) setMissingKey(null)
                                  setParamVals((s) => ({ ...s, [key]: e.target.value }))
                                }}
                                placeholder={p.placeholder}
                              />
                            </div>
                          )
                        })}
                      </div>

                      {selectedArgs.length > 0 && (
                        <div className="qt-bof-pack-strip" title="按 BeaconDataParse 二进制协议编码后的字段序列预览">
                          <span>打包序列 ({selectedArgs.length} 字段):</span>
                          {selectedArgs.map((arg, idx) => (
                            <span
                              key={idx}
                              className={`qt-bof-badge ${arg.kind === 'int' ? 'warn' : ''}`}
                            >
                              #{idx + 1} {arg.kind === 'int' ? 'int32' : 'str'}: {arg.value.replace(/\0/g, ' \\0 ')}
                            </span>
                          ))}
                        </div>
                      )}
                    </>
                  )}
                </div>

                {/* Operational Notes & Warnings */}
                {(selected.meta.notes || []).length > 0 && (
                  <div className="qt-bof-sec">
                    <div className="qt-bof-sec-hd">
                      <span>实战约束与注意事项</span>
                    </div>
                    <div className="qt-bof-notes">
                      {(selected.meta.notes || []).filter((n) => n.startsWith('★')).map((n, i) => (
                        <div key={`hot-${i}`} className="qt-bof-note-hot">{n}</div>
                      ))}
                      {(selected.meta.notes || []).some((n) => !n.startsWith('★')) && (
                        <ul className="qt-bof-note-list">
                          {(selected.meta.notes || []).filter((n) => !n.startsWith('★')).map((n, i) => (
                            <li key={`n-${i}`}>{n}</li>
                          ))}
                        </ul>
                      )}
                    </div>
                  </div>
                )}
              </div>

              {/* Anchored Compile Output Drawer */}
              {out && (
                <div className={`qt-bof-drawer ${out.bad ? 'bad' : ''}`}>
                  <div className="qt-bof-drawer-hd">
                    <span>{out.title}</span>
                    <span style={{ flex: 1 }} />
                    <button
                      type="button"
                      className="qt-bof-pill"
                      onClick={() => void copyText(out.text).then((ok) => pushToast(ok ? 'ok' : 'err', ok ? '已复制' : t('复制失败')))}
                    >
                      {t('复制')}
                    </button>
                    <button type="button" className="qt-bof-pill" onClick={() => setOut(null)}>{t('关闭')}</button>
                  </div>
                  <pre className="qt-bof-drawer-pre">{out.text || '(空)'}</pre>
                </div>
              )}

              {/* Sticky Execution Dock */}
              <div className="qt-bof-exec-dock">
                <div className="qt-bof-cmd-preview">
                  <code className="qt-bof-cmd-code" title={selectedCmdline}>{selectedCmdline}</code>
                  <button
                    type="button"
                    className="qt-bof-btn"
                    onClick={() => void copyCmd(selected)}
                  >
                    {t('复制命令')}
                  </button>
                </div>

                <div className="qt-bof-exec-actions">
                  <label
                    className="qt-bof-async-chk"
                    title={t('异步执行（长任务）：走 COMMAND_EXEC_BOF_ASYNC，不阻塞 agent 的命令循环')}
                  >
                    <input
                      type="checkbox"
                      checked={!!asyncOn[selected.name]}
                      onChange={(e) => setAsyncOn((s) => ({ ...s, [selected.name]: e.target.checked }))}
                    />
                    <span>异步模式</span>
                    <code>{asyncMark()}</code>
                  </label>

                  <span style={{ fontSize: 11, color: 'var(--qt-text-muted)' }}>
                    {selectedAgent
                      ? `目标: ${selectedAgent.a_computer || selectedAgent.a_id} (${selectedAgent.a_username || '-'})`
                      : '当前无在线主机，无法下发'}
                  </span>

                  <span style={{ flex: 1 }} />

                  <button
                    type="button"
                    className="qt-bof-btn primary"
                    disabled={busy === selected.name || !effectiveAgent || targetPlatformMismatch || !!unsupportedReason}
                    onClick={() => void runBof(selected)}
                    title={unsupportedReason || (!effectiveAgent ? '没有在线主机' : targetPlatformMismatch ? '当前所选主机不是 Linux，请切换目标主机' : `在 ${effectiveAgent} 上执行 (Ctrl+Enter)`)}
                  >
                    <img className="qt-ico" src={qtAsset('icons/start_64dp.png')} alt="" style={{ filter: 'brightness(10)' }} />
                    <span>{busy === selected.name ? '下发中…' : `${t('执行')} BOF (Ctrl+↵)`}</span>
                  </button>
                </div>
              </div>
            </>
          ) : (
            <div style={{ flex: 1, display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
              <EmptyState
                icon="⎔"
                title={rows.length ? t('未选中模块') : t('服务端还没有 BOF')}
                hint={t('从左侧列表选择一个 BOF 模块，或拖拽 .c / .o 文件到此处上传')}
              />
            </div>
          )}
        </div>
      </div>
      ) : (
      <div className="qt-bof-split" ref={splitRef}>
        <ExtBofListPane
          grouped={extGrouped}
          selectedKey={selectedExt?.key || ''}
          selectedSubName={activeExtSub?.name || ''}
          searchQuery={search}
          recentSet={recentExtSet}
          favSet={favExtSet}
          collapsedSet={collapsedCats}
          expandedSet={expandedExtKeys}
          splitPct={splitPct}
          hasAny={extCatalog.items.length > 0}
          platform={activePlatform}
          onSelect={onSelectExtRow}
          onSelectSub={onSelectExtSub}
          onToggleCollapse={onToggleCollapse}
          onToggleExpand={onToggleExpandExt}
          onToggleFav={onToggleFavExt}
          onContext={onExtRowContext}
          onResetFilters={onResetFilters}
        />

        <div
          className={`qt-vsplit ${draggingSplit ? 'dragging' : ''}`}
          role="separator"
          aria-orientation="vertical"
          title="拖拽调整左右栏宽度，双击还原"
          onMouseDown={() => setDraggingSplit(true)}
          onDoubleClick={() => {
            setSplitPct(44)
            try { localStorage.setItem(SPLIT_KEY, '44') } catch { /* ignore */ }
          }}
        />

        {/* Right Detail Inspector & Execution Dock (Windows / macOS Extension-Kit BOFs) */}
        <div className="qt-bof-detail-pane">
          {selectedExt && activeExtCmd ? (
            <>
              <div className="qt-bof-detail-scroll">
                <div className="qt-bof-detail-hd">
                  <div className="qt-bof-detail-title-row">
                    <span className="qt-bof-detail-name">{selectedExt.cmd.name}</span>
                    <span className="qt-bof-badge info">{selectedExt.suite}</span>
                    <span className="qt-bof-badge">{selectedExt.purpose}</span>
                    {selectedExt.batchAgent && <span className="qt-bof-badge">{selectedExt.batchAgent}</span>}
                    {selectedExt.scriptName && <span className="qt-bof-badge">{selectedExt.scriptName}</span>}
                    {selectedExt.cmd.has_pre_hook && <span className="qt-bof-badge ok">COFF · AxScript Hook</span>}
                  </div>
                  <div className="qt-bof-detail-desc">
                    {selectedExt.cmd.description || selectedExt.cmd.message || 'Windows Extension-Kit BOF 命令'}
                  </div>
                </div>

                {/* Subcommand Selector with live sub-filter for large suites (e.g. ldap 51, mssql 28, kerbeus 16) */}
                {extSubs.length > 0 && (() => {
                  const sf = subFilter.trim().toLowerCase()
                  const shownSubs = sf
                    ? extSubs.filter((s) => `${s.name} ${s.description || ''}`.toLowerCase().includes(sf))
                    : extSubs
                  return (
                    <div className="qt-bof-sec">
                      <div className="qt-bof-sec-hd">
                        <span>子命令 ({shownSubs.length}/{extSubs.length})</span>
                        {extSubs.length > 8 && (
                          <input
                            className="qt-bof-sub-filter"
                            value={subFilter}
                            onChange={(e) => setSubFilter(e.target.value)}
                            placeholder="筛选子命令…"
                          />
                        )}
                      </div>
                      <div className="qt-bof-sub-strip">
                        <div className="qt-bof-pills" style={{ flexWrap: 'wrap', flexShrink: 1, maxWidth: '100%' }}>
                          {shownSubs.map((sub) => (
                            <button
                              key={sub.name}
                              type="button"
                              className={`qt-bof-pill ${activeExtSub?.name === sub.name ? 'active' : ''}`}
                              onClick={() => {
                                setExtSubByKey((p) => ({ ...p, [selectedExt.key]: sub.name }))
                                setMissingKey(null)
                              }}
                              title={sub.description || sub.name}
                            >
                              {sub.name}
                            </button>
                          ))}
                        </div>
                      </div>
                      {activeExtSub?.description && (
                        <div style={{ fontSize: 11.5, color: 'var(--qt-text-muted)' }}>{activeExtSub.description}</div>
                      )}
                    </div>
                  )
                })()}

                {/* Parameters Form */}
                <div className="qt-bof-sec">
                  <div className="qt-bof-sec-hd">
                    <span>
                      运行参数
                      {(activeExtCmd.args || []).length > 0
                        ? ` (${(activeExtCmd.args || []).length})`
                        : '（无参数）'}
                    </span>
                    {hasAnyExtArgVal && (
                      <button type="button" className="qt-bof-pill" onClick={clearSelectedParams}>清空参数</button>
                    )}
                  </div>

                  {(activeExtCmd.args || []).length === 0 ? (
                    <div style={{ fontSize: 11.5, color: 'var(--qt-text-muted)' }}>
                      该 Windows BOF 命令无需额外参数，可直接执行。
                    </div>
                  ) : (
                    <div className="qt-bof-param-grid">
                      {(activeExtCmd.args || []).map((a, idx) => {
                        const argKey = `${extBaseArgKey}.${idx}`
                        if (a.flag && !a.name) {
                          return (
                            <label key={argKey} className="qt-bof-flag-row" title={a.description || a.mark}>
                              <input
                                type="checkbox"
                                checked={extArgVals[argKey] === '1'}
                                onChange={(e) => {
                                  setExtCustomCmd((p) => { const n = { ...p }; delete n[extBaseArgKey]; return n })
                                  setExtArgVals((p) => ({ ...p, [argKey]: e.target.checked ? '1' : '' }))
                                }}
                              />
                              <span>{a.description || a.mark || '启用开关'}</span>
                              {a.mark && <code>{a.mark}</code>}
                            </label>
                          )
                        }
                        const inputId = `bof-ext-arg-${argKey}`
                        const missId = `ext:${argKey}`
                        const desc = (a.description || '').trim()
                        const isFileArg = String(a.type || '').toLowerCase() === 'file'
                        const isLongDesc = desc.length > 36 || desc.includes('\n')
                        const fileHint = isFileArg
                          ? `提示：FILE 参数由服务端读取，文件须位于 Extenders / AxScripts 沙箱目录内${winExecBase ? `（如 ${winExecBase}）` : ''}。`
                          : ''
                        const helpLines = [isLongDesc ? desc : '', fileHint].filter(Boolean).join('\n')
                        const placeholder = !isLongDesc && desc
                          ? desc
                          : (isFileArg
                              ? '服务端沙箱路径（Extenders / AxScripts 内）'
                              : ((a.default_used && a.default_value != null ? String(a.default_value) : '') || (desc.split('\n')[0] || a.name || a.mark)))
                        return (
                          <div key={argKey} style={{ display: 'contents' }}>
                            <label className="qt-bof-param-lbl" htmlFor={inputId} title={desc || a.name}>
                              <span>
                                {a.name || a.mark}
                                {a.required && <span className="qt-bof-req"> *</span>}
                              </span>
                              <span style={{ display: 'inline-flex', gap: 4 }}>
                                {a.flag && a.mark && <span className="qt-bof-badge info">{a.mark}</span>}
                                {a.type && <span className={`qt-bof-badge ${a.type === 'int' ? 'warn' : isFileArg ? 'info' : ''}`}>{a.type}</span>}
                              </span>
                            </label>
                            <input
                              id={inputId}
                              ref={(el) => { paramRefs.current[missId] = el }}
                              className={`qt-bof-input ${missingKey === missId ? 'invalid' : ''}`}
                              value={extArgDisplayValue(extArgVals, extBaseArgKey, a, idx)}
                              onChange={(e) => {
                                if (missingKey === missId) setMissingKey(null)
                                const v = e.target.value
                                setExtCustomCmd((p) => { const n = { ...p }; delete n[extBaseArgKey]; return n })
                                setExtArgVals((p) => ({ ...p, [argKey]: v }))
                              }}
                              placeholder={placeholder}
                            />
                            {helpLines && <div className="qt-bof-arg-help">{helpLines}</div>}
                          </div>
                        )
                      })}
                    </div>
                  )}
                </div>

                {/* Authoritative Example */}
                {activeExtCmd.example && (
                  <div className="qt-bof-sec">
                    <div className="qt-bof-sec-hd">
                      <span>权威示例（由服务端 .axs 预钩子展开为 BOF 调用）</span>
                      <button
                        type="button"
                        className="qt-bof-pill"
                        onClick={() => setExtCustomCmd((p) => ({ ...p, [extBaseArgKey]: activeExtCmd.example }))}
                      >
                        填入命令框
                      </button>
                    </div>
                    <div className="qt-bof-example-box">
                      <code>{activeExtCmd.example}</code>
                      <button
                        type="button"
                        className="qt-bof-btn"
                        onClick={() => void copyText(activeExtCmd.example).then((ok) => pushToast(ok ? 'ok' : 'err', ok ? '示例已复制' : t('复制失败')))}
                      >
                        复制示例
                      </button>
                    </div>
                  </div>
                )}

                {extAgentBatchMismatch && selectedAgent && (
                  <div className="qt-bof-note-hot">
                    ★ 当前主机类型是 <b>{selectedAgent.a_name}</b>，但仅检测到 <b>{selectedExt.batchAgent}</b> 注册了该 Windows Extension-Kit；请切到对应类型主机或检查该 agent 的 `.axs` 加载状态。
                  </div>
                )}
              </div>

              {/* Sticky Windows / Ext Execution Dock */}
              <div className="qt-bof-exec-dock">
                <div className="qt-bof-cmd-preview">
                  <input
                    className="qt-bof-cmd-input"
                    value={finalExtCmdline}
                    onChange={(e) => setExtCustomCmd((p) => ({ ...p, [extBaseArgKey]: e.target.value }))}
                    placeholder={autoExtCmdline}
                    spellCheck={false}
                    aria-label="Command preview"
                  />
                  {extBaseArgKey in extCustomCmd && (
                    <button
                      type="button"
                      className="qt-bof-pill"
                      onClick={() => setExtCustomCmd((p) => { const n = { ...p }; delete n[extBaseArgKey]; return n })}
                      title="恢复为上方参数自动拼接的命令"
                    >
                      还原
                    </button>
                  )}
                  <button
                    type="button"
                    className="qt-bof-btn"
                    onClick={() => void copyText(finalExtCmdline).then((ok) => pushToast(ok ? 'ok' : 'err', ok ? '命令已复制' : t('复制失败')))}
                  >
                    {t('复制命令')}
                  </button>
                </div>

                <div className="qt-bof-exec-actions">
                  <span style={{ fontSize: 11, color: 'var(--qt-text-muted)' }}>
                    {selectedAgent
                      ? `目标: ${selectedAgent.a_computer || selectedAgent.a_id} (${selectedAgent.a_username || '-'} · ${platformLabel(detectPlatform(selectedAgent))})`
                      : '当前无在线主机，仅支持预览/复制命令'}
                  </span>
                  <span style={{ flex: 1 }} />
                  <button
                    type="button"
                    className="qt-bof-btn primary"
                    disabled={busy === selectedExt.key || !effectiveAgent || targetPlatformMismatch || extAgentBatchMismatch || !!unsupportedReason}
                    onClick={() => void runExtBof(selectedExt)}
                    title={unsupportedReason || (!effectiveAgent ? '没有在线主机' : targetPlatformMismatch ? '当前主机系统与插件平台不匹配' : `在 ${effectiveAgent} 上执行 (Ctrl+Enter)`)}
                  >
                    <img className="qt-ico" src={qtAsset('icons/start_64dp.png')} alt="" style={{ filter: 'brightness(10)' }} />
                    <span>{busy === selectedExt.key ? '下发中…' : `${t('执行')} BOF (Ctrl+↵)`}</span>
                  </button>
                </div>
              </div>
            </>
          ) : (
            <div style={{ flex: 1, display: 'flex', alignItems: 'center', justifyContent: 'center', padding: 20 }}>
              <EmptyState
                icon="⎔"
                title={activePlatform === 'macos' ? '当前暂无 macOS BOF 插件' : `未找到 ${platformLabel(activePlatform)} BOF 插件`}
                hint={activePlatform === 'macos'
                  ? 'macOS 暂未加载 Extension-Kit BOF 套件；可切回 Windows 或 Linux 主机使用对应平台的 BOF。'
                  : '请确认团队端 profile 已加载 Extension-Kit，或点击右上角「刷新」重新拉取命令树。'}
                action={(
                  <div style={{ display: 'flex', gap: 8 }}>
                    <button type="button" className="qt-bof-btn primary" onClick={() => switchPlatform('windows')}>切到 Windows BOF</button>
                    <button type="button" className="qt-bof-btn" onClick={() => switchPlatform('linux')}>切到 Linux BOF</button>
                  </div>
                )}
              />
            </div>
          )}
        </div>
      </div>
      )}

      {/* ── 4. Compact Qt Status Footer ── */}
      {activePlatform === 'linux' ? (
        <div className="qt-bof-foot">
          <span
            className="qt-bof-foot-base"
            title="点击修改服务端 BOF 基路径"
            onClick={() => setBaseOpen((v) => !v)}
          >
            Base: {effectiveBase || '(自动/文件名直传)'}
          </span>
          <span style={{ flex: 1 }} />
          <span className="mono" style={{ whiteSpace: 'nowrap' }}>
            {offline
              ? `离线兜底 ${rows.length}`
              : `Linux ELF ${rows.length} · 可执行 ${nObj}${nStale ? ` · 源码已改 ${nStale}` : ''}${nOther ? ` · 未登记 ${nOther}` : ''}`}
          </span>
          <span style={{ whiteSpace: 'nowrap' }}>· 拖拽 .c/.o 上传</span>
        </div>
      ) : (
        <div className="qt-bof-foot">
          <span className="mono" style={{ whiteSpace: 'nowrap' }}>
            {platformLabel(activePlatform)} Extension-Kit
            {extCatalog.batchAgent ? ` (${extCatalog.batchAgent})` : ''}
          </span>
          <span style={{ flex: 1 }} />
          <span className="mono" style={{ whiteSpace: 'nowrap' }}>
            套件 {extSuites.length} · 命令 {extCatalog.items.length} · 含子命令 {extLeafCountTotal} 项 · AxScript PreHook 执行
          </span>
        </div>
      )}

      {/* Linux Row Context Menu */}
      {ctx && (
        <div
          className="ctx"
          style={{ left: Math.min(ctx.x, window.innerWidth - 210), top: Math.min(ctx.y, window.innerHeight - 220) }}
          onClick={(e) => e.stopPropagation()}
        >
          <button type="button" disabled={!effectiveAgent || targetPlatformMismatch || !!unsupportedReason} onClick={() => { void runBof(ctx.row); setCtx(null) }}>
            <span>{t('执行')} {ctx.row.name}</span>
          </button>
          <button type="button" onClick={() => { void copyCmd(ctx.row); setCtx(null) }}>
            <span>{t('复制命令')}</span>
          </button>
          <button
            type="button"
            disabled={!api || (!ctx.row.src && !offline)}
            onClick={() => { void compileOne(ctx.row); setCtx(null) }}
          >
            <span>重编 .o</span>
          </button>
          <div className="sep" />
          <button
            type="button"
            onClick={() => {
              void copyText(fullPath(ctx.row)).then((ok) => pushToast(ok ? 'ok' : 'err', ok ? '路径已复制' : t('复制失败')))
              setCtx(null)
            }}
          >
            <span>复制服务端路径</span>
          </button>
          {ctx.row.obj_sha256 && (
            <button
              type="button"
              onClick={() => {
                void copyText(ctx.row.obj_sha256).then((ok) => pushToast(ok ? 'ok' : 'err', ok ? 'SHA256 已复制' : t('复制失败')))
                setCtx(null)
              }}
            >
              <span>复制 SHA256</span>
            </button>
          )}
        </div>
      )}

      {/* Windows / Extension-Kit Row Context Menu */}
      {extCtx && (
        <div
          className="ctx"
          style={{ left: Math.min(extCtx.x, window.innerWidth - 210), top: Math.min(extCtx.y, window.innerHeight - 160) }}
          onClick={(e) => e.stopPropagation()}
        >
          <button
            type="button"
            disabled={!effectiveAgent || targetPlatformMismatch || !!unsupportedReason || (!!extCtx.item.batchAgent && selectedAgent?.a_name !== extCtx.item.batchAgent)}
            onClick={() => { void runExtBof(extCtx.item); setExtCtx(null) }}
          >
            <span>{t('执行')} {extCtx.item.cmd.name}</span>
          </button>
          <button
            type="button"
            onClick={() => {
              const txt = buildExtCmdline(extCtx.item, extSubByKey[extCtx.item.key] || '', extArgVals)
              void copyText(txt).then((ok) => pushToast(ok ? 'ok' : 'err', ok ? '命令已复制' : t('复制失败')))
              setExtCtx(null)
            }}
          >
            <span>{t('复制命令')}</span>
          </button>
          {extCtx.item.cmd.example && (
            <button
              type="button"
              onClick={() => {
                void copyText(extCtx.item.cmd.example).then((ok) => pushToast(ok ? 'ok' : 'err', ok ? '示例已复制' : t('复制失败')))
                setExtCtx(null)
              }}
            >
              <span>复制示例</span>
            </button>
          )}
        </div>
      )}

      {/* Drag & Drop Upload Overlay */}
      {dragOver && activePlatform === 'linux' && (
        <div className="qt-drop-overlay">
          <div>释放鼠标以上传 BOF (.c 自动编译 / .o 直接投递)</div>
        </div>
      )}
    </div>
  )
})
