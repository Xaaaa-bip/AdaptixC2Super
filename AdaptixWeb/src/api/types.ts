export type Packet = { type: number; [k: string]: any }

export type Agent = {
  a_id: string
  a_name?: string
  a_listener?: string
  a_external_ip?: string
  a_internal_ip?: string
  a_domain?: string
  a_computer?: string
  a_username?: string
  a_os?: number
  a_os_desc?: string
  a_process?: string
  a_pid?: string
  a_tid?: string
  a_tags?: string
  a_mark?: string
  a_create_time?: number
  a_last_tick?: number
  a_sleep?: number
  a_jitter?: number
  a_elevated?: boolean
  a_arch?: string
  a_acp?: number
  a_oemcp?: number
}

export type Listener = {
  l_name: string
  l_reg_name?: string
  l_protocol?: string
  l_type?: string
  l_bind_host?: string
  l_bind_port?: string
  l_agent_addr?: string
  l_status?: string
  a_create_time?: number
  l_data?: string
}

export type Task = {
  a_task_id?: string
  a_id?: string
  a_client?: string
  a_user?: string
  a_computer?: string
  a_cmdline?: string
  a_start_time?: number
  a_finish_time?: number
  a_message?: string
  a_text?: string
  a_completed?: boolean
}

export type DownloadItem = {
  d_file_id?: string
  d_agent_id?: string
  d_user?: string
  d_computer?: string
  d_filename?: string
  d_size?: number
  d_file?: string
}

export type ChatMessage = {
  id: string
  username: string
  message: string
  date: number
}

export type Pivot = {
  p_pivot_id: string
  p_pivot_name?: string
  p_parent_agent_id?: string
  p_child_agent_id?: string
}

export type ConsoleLine = {
  id: string
  agentId: string
  kind: 'local' | 'info' | 'ok' | 'err' | 'task'
  text: string
  ts: number
}

export type AppLog = {
  id: string
  level: 'info' | 'ok' | 'err' | 'warn'
  text: string
  ts: number
}

export const P = {
  SYNC_START: 0x11,
  SYNC_FINISH: 0x12,
  NOTIFICATION: 0x13,
  SYNC_BATCH: 0x14,
  SYNC_CATEGORY_BATCH: 0x15,
  LISTENER_START: 0x31,
  LISTENER_EDIT: 0x32,
  LISTENER_STOP: 0x33,
  AGENT_NEW: 0x41,
  AGENT_UPDATE: 0x42,
  AGENT_REMOVE: 0x43,
  AGENT_TICK: 0x44,
  AGENT_TASK_SYNC: 0x49,
  AGENT_TASK_UPDATE: 0x4a,
  AGENT_TASK_SEND: 0x4b,
  AGENT_TASK_REMOVE: 0x4c,
  CHAT_MESSAGE: 0x18,
  // 插件自定义服务的广播（plugin → TsServiceSendDataAll → SyncPackerServiceData）。
  // 载荷是 {type, service, data}，其中 data 是**字符串**，里面才是 JSON。
  SERVICE_DATA: 0x19,
  DOWNLOAD_CREATE: 0x51,
  DOWNLOAD_UPDATE: 0x52,
  DOWNLOAD_DELETE: 0x53,
  PIVOT_CREATE: 0x71,
  PIVOT_DELETE: 0x72,
  CONSOLE_LOCAL: 0x67,
  CONSOLE_ERROR: 0x68,
  CONSOLE_OUT: 0x69,
  CONSOLE_TASK_SYNC: 0x6a,
  CONSOLE_TASK_UPD: 0x6b,
  BROWSER_DISKS: 0x61,
  BROWSER_FILES: 0x62,
  BROWSER_FILES_STATUS: 0x63,
  BROWSER_PROCESS: 0x64,
} as const

export const DEFAULT_SUBSCRIPTIONS = [
  'chat_history', 'chat_realtime',
  'downloads_history', 'downloads_realtime',
  'screenshot_history', 'screenshot_realtime',
  'credentials_history', 'credentials_realtime',
  'targets_history', 'targets_realtime',
  'console_history', 'tasks_history', 'tasks_manager',
  'notifications', 'tunnels', 'agents', 'pivots',
]

/* ---- axscript 命令树（POST /api/axscript/commands） ----
 *
 * 形状对齐 AdaptixServer/core/axscript/types.go + registry.go 的 CommandBatch。
 * 它是各 agent 的 ax_config.axs 里 create_command / addArgString / addSubCommands
 * 的产物，和团队端派发用的是同一棵注册树，所以不会像前端手抄的命令表那样漂移。
 *
 * os 取值与 axscript.OsWindows/OsLinux/OsMac 一致，也与 AgentData.a_os 一致。
 */
export type CmdArgDef = {
  type: string
  name: string
  required: boolean
  flag: boolean
  mark: string
  description: string
  default_used: boolean
  default_value?: unknown
}

export type CmdDef = {
  name: string
  message: string
  description: string
  example: string
  args?: CmdArgDef[]
  subcommands?: CmdDef[]
  /** 服务端在 CommandDef 上带出来的三个能力位（core/axscript/types.go）。
   *  实测 wire 上是 snake_case（has_pre_hook / has_post_hook / has_handler），
   *  值为 bool。**has_pre_hook=true 的含义不是「有个钩子」这么轻**：Extension-Kit
   *  那批 BOF 命令靠它把 cmdline 展开成 `execute bof …_bin/<file>.<arch>.o`，
   *  也就是说这条命令的参数写法由钩子决定，不由 args 表决定。 */
  has_pre_hook?: boolean
  has_post_hook?: boolean
  has_handler?: boolean
}

export type CmdGroup = {
  group_name: string
  group_description?: string
  script_name: string
  commands: CmdDef[]
}

export type CmdBatch = {
  /** axscript.SourceType：0=agent 1=profile 2=user（registry.go:11，序列化成数字） */
  source: number
  /** agent_name（config.yaml），与 AgentData.a_name 同源 */
  agent: string
  /** 监听器**注册名**（config.yaml 的 listeners: 项，如 KharonHTTP） */
  listener: string
  os: number
  groups: CmdGroup[]
}

/* ---- 生成参数表（POST /api/agent/build/schema） ----
 *
 * 形状对齐各 agent 的 ax_config.axs 里 `function BuildSchema(listener_type)` 的
 * 返回值：服务端原样 Export() 后 JSON 序列化下发，**不是 `{ok,message}` 包装**。
 *
 * 为什么要有它：载荷页原来的参数表是前端手抄的，只覆盖 beacon 的 7 个键，而
 * pl_main.go 的 GenerateConfig 实际读 32 个 —— killdate / DNS 模式 / 代理全套 /
 * 图标 / sideloading / stager_ts_* 插件真的在消费，Web 界面却发不出去。
 * 现在字段表由插件自己声明，和 GenerateUI（Qt 那条路）并列、互不影响。
 *
 * 拿不到表（返回 `{}`）不是错误：那是「这个 agent 还没迁移」的契约回退，
 * 控制台照旧走内置界面（见 api/buildSchema.ts 的 normalize）。
 */
export type BuildSchemaField = {
  /** 就是 GenerateConfig 的 json tag，直接进 config，前端不做二次映射 */
  key: string
  label: string
  /** select | text | number | bool | file；认不出的按 text 渲染 */
  type: string
  /** select 的取值 */
  options?: string[]
  /** 没写就别猜 —— 见 api/buildSchema.ts 里关于「不下发」的说明 */
  default?: unknown
  min?: number
  max?: number
  placeholder?: string
  hint?: string
  /** type=file 用，HTML accept 串，如 ".ico" */
  accept?: string
  /** 分组标题；同组字段连续，收进一个可折叠块 */
  group?: string
  /** 二者同时给出时都要满足 */
  show_if?: { format_in?: string[]; listener_in?: string[] }
}

export type BuildSchema = {
  version: number
  fields: BuildSchemaField[]
}
