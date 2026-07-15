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
