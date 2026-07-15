export type LoginResult = { access_token: string; refresh_token: string }

function joinUrl(base: string, path: string) {
  if (path.startsWith('http')) return path
  const b = base.replace(/\/+$/, '')
  const p = path.startsWith('/') ? path : `/${path}`
  if (b.endsWith('/api') && p.startsWith('/api/')) return b + p.slice(4)
  return b + p
}

export class ApiClient {
  baseUrl: string
  accessToken = ''
  refreshToken = ''

  constructor(baseUrl: string) {
    this.baseUrl = baseUrl.replace(/\/+$/, '')
  }

  /** 可选：token 变更时写 localStorage（F5 恢复会话） */
  onTokensChange?: (access: string, refresh: string) => void

  setTokens(access: string, refresh: string) {
    this.accessToken = access
    this.refreshToken = refresh
    try { this.onTokensChange?.(access, refresh) } catch { /* ignore */ }
  }

  async login(username: string, password: string): Promise<LoginResult> {
    const res = await fetch(joinUrl(this.baseUrl, '/api/login'), {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ username, password, version: 'v1.2' }),
    })
    const data = await res.json().catch(() => ({}))
    if (!res.ok || !data.access_token) {
      throw new Error(data.message || data.error || `login failed HTTP ${res.status}`)
    }
    this.setTokens(data.access_token, data.refresh_token || '')
    return data
  }

  async refreshAccess(): Promise<string> {
    if (!this.refreshToken) throw new Error('no refresh token')
    const res = await fetch(joinUrl(this.baseUrl, '/api/refresh'), {
      method: 'POST',
      headers: { Authorization: `Bearer ${this.refreshToken}` },
    })
    const data = await res.json().catch(() => ({}))
    if (!data.access_token) throw new Error(data.message || 'refresh failed')
    // 保留 refresh；部分实现会轮换 refresh_token
    this.setTokens(data.access_token, data.refresh_token || this.refreshToken)
    return data.access_token
  }

  private async request<T = any>(method: string, path: string, body?: any, retry = true): Promise<T> {
    const headers: Record<string, string> = {}
    if (this.accessToken) headers.Authorization = `Bearer ${this.accessToken}`
    let payload: string | undefined
    if (body !== undefined) {
      headers['Content-Type'] = 'application/json'
      payload = JSON.stringify(body)
    }
    const res = await fetch(joinUrl(this.baseUrl, path), { method, headers, body: payload })
    // 401 → try refresh once
    if (res.status === 401 && retry && this.refreshToken) {
      try {
        await this.refreshAccess()
        return this.request<T>(method, path, body, false)
      } catch {
        throw new Error('session expired')
      }
    }
    const text = await res.text()
    let data: any = null
    try {
      data = text ? JSON.parse(text) : null
    } catch {
      if (retry && this.refreshToken) {
        try {
          await this.refreshAccess()
          return this.request<T>(method, path, body, false)
        } catch { /* fallthrough */ }
      }
      throw new Error(`non-JSON response HTTP ${res.status}`)
    }
    if (data && data.ok === false) throw new Error(data.message || 'request failed')
    return data as T
  }

  get<T = any>(path: string) { return this.request<T>('GET', path) }
  post<T = any>(path: string, body?: any) { return this.request<T>('POST', path, body) }

  async generateOtp(type: string, data: any): Promise<string> {
    const res = await this.post<{ ok: boolean; message: string }>('/api/otp/generate', { type, data })
    if (!res?.message) throw new Error('empty otp')
    return res.message
  }

  sync() { return this.post('/api/sync', {}) }
  listAgents() { return this.get<any[]>('/api/agent/list') }
  listListeners() { return this.get<any[]>('/api/listener/list') }

  listTasks(agentId?: string) {
    // server requires agent_id; without it returns {ok:false}
    if (!agentId) return Promise.resolve([])
    const q = `?agent_id=${encodeURIComponent(agentId)}&limit=200&offset=0`
    return this.get<any[]>(`/api/agent/task/list${q}`)
  }
  listDownloads() {
    return this.get<any[]>('/api/download/list').then((d) => (Array.isArray(d) ? d : []))
  }

  createListener(name: string, type: string, config: object) {
    return this.post('/api/listener/create', { name, type, config: JSON.stringify(config) })
  }
  editListener(name: string, type: string, config: object) {
    return this.post('/api/listener/edit', { name, type, config: JSON.stringify(config) })
  }
  stopListener(name: string, type: string) {
    return this.post('/api/listener/stop', { name, type })
  }
  pauseListener(name: string, type: string) {
    return this.post('/api/listener/pause', { name, type })
  }
  resumeListener(name: string, type: string) {
    return this.post('/api/listener/resume', { name, type })
  }

  /**
   * Upload a browser File to teamserver temp store via OTP:
   * 1) POST /api/otp/generate { type: tmp_upload, data: { id } }
   * 2) POST /api/otp/upload/temp?otp=... multipart field "file"
   * Returns fileId used as __file_ref for command/execute.
   */
  async uploadTempFile(file: File, onProgress?: (pct: number) => void): Promise<string> {
    const fileId = Array.from(crypto.getRandomValues(new Uint8Array(12)))
      .map((b) => b.toString(16).padStart(2, '0')).join('')
    const otp = await this.generateOtp('tmp_upload', { id: fileId })
    const url = joinUrl(this.baseUrl, `/api/otp/upload/temp?otp=${encodeURIComponent(otp)}`)
    await new Promise<void>((resolve, reject) => {
      const xhr = new XMLHttpRequest()
      xhr.open('POST', url, true)
      xhr.onload = () => {
        if (xhr.status >= 200 && xhr.status < 300) resolve()
        else reject(new Error(`upload failed HTTP ${xhr.status}: ${xhr.responseText || ''}`))
      }
      xhr.onerror = () => reject(new Error('upload network error'))
      if (xhr.upload && onProgress) {
        xhr.upload.onprogress = (ev) => {
          if (ev.lengthComputable) onProgress(Math.round((ev.loaded / ev.total) * 100))
        }
      }
      const fd = new FormData()
      fd.append('file', file, file.name)
      xhr.send(fd)
    })
    return fileId
  }

  /**
   * Structured upload: local file already in tmp store as fileId.
   * args.local_file = { __file_ref: fileId } is resolved server-side to base64 content.
   */
  uploadToAgent(agentId: string, fileId: string, localName: string, remotePath: string) {
    const cmdline = `upload ${localName} ${remotePath}`
    return this.commandExecute(agentId, cmdline, {
      command: 'upload',
      local_file: { __file_ref: fileId },
      remote_path: remotePath,
      message: 'Task: upload file',
    }, false)
  }
  generateAgent(agent: string, listener_name: string[], config: object) {
    return this.post<{ ok: boolean; message: string }>('/api/agent/generate', {
      agent, listener_name, config: JSON.stringify(config),
    })
  }

  /** WebDelivery 插件：一键上线配置（对齐 Qt DialogWebDelivery） */
  webDeliveryStatus() {
    return this.post<{
      ok?: boolean
      script_route?: string
      payload_route?: string
      listener?: string
      agent_name?: string
      format?: string
      cache_entries?: number
      c2_host_url?: string
      sleep?: string
      jitter?: number
      cache_ttl_min?: number
      message?: string
    }>('/api/webdelivery/status', {})
  }

  webDeliveryConfig(cfg: {
    script_route: string
    payload_route: string
    listener: string
    agent_name: string
    format: string
    sleep: string
    jitter: number
    cache_ttl_min: number
    c2_host_url: string
    arch?: string
  }) {
    return this.post<{ ok?: boolean; message?: string }>('/api/webdelivery/config', cfg)
  }
  static decodeGeneratePayload(message: string): { filename: string; bytes: Uint8Array } {
    const idx = message.indexOf(':')
    if (idx < 0) throw new Error('unexpected generate payload')
    const filename = atob(message.slice(0, idx))
    const bin = atob(message.slice(idx + 1))
    const bytes = new Uint8Array(bin.length)
    for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i)
    return { filename, bytes }
  }
  removeAgents(ids: string[]) { return this.post('/api/agent/remove', { agent_id_array: ids }) }
  setTag(ids: string[], tag: string) { return this.post('/api/agent/set/tag', { agent_id_array: ids, tag }) }
  setMark(ids: string[], mark: string) { return this.post('/api/agent/set/mark', { agent_id_array: ids, mark }) }
  setColor(ids: string[], background: string, foreground: string, reset = false) {
    return this.post('/api/agent/set/color', { agent_id_array: ids, background, foreground, reset })
  }
  /** 清除服务端保存的该主机控制台历史（本机 UI 也需 clearConsole） */
  consoleRemove(ids: string[]) {
    return this.post('/api/agent/console/remove', { agent_id_array: ids })
  }
  /** Qt DialogAgentData → /agent/update/data（展示字段，不含 sleep 任务） */
  updateAgentData(agentId: string, data: Record<string, any>) {
    return this.post('/api/agent/update/data', { agent_id: agentId, ...data })
  }
  chatSend(message: string) {
    return this.post('/api/chat/send', { message })
  }
  downloadDelete(fileIds: string[]) {
    return this.post('/api/download/delete', { file_id_array: fileIds })
  }

  /**
   * command/raw: server-side AxScript parse (console commands).
   * File/Process Browser MUST use commandBrowser() so ui=true → TASK_TYPE_BROWSER.
   */
  commandRaw(agentId: string, cmdline: string) {
    return this.post('/api/agent/command/raw', { id: agentId, cmdline })
  }

  /**
   * Structured command via /api/agent/command/execute.
   * args must include { command, subcommand?, path?, pid?, file?, ... } as AxScript packs them.
   * ui=true marks browser tasks so results come as BROWSER_* sync packets.
   */
  commandExecute(agentId: string, cmdline: string, args: Record<string, any>, ui = false) {
    return this.post('/api/agent/command/execute', {
      id: agentId,
      ui,
      cmdline,
      data: JSON.stringify(args),
      ax_hook_id: '',
      ax_handler_id: '',
      wait_answer: false,
    })
  }

  /** File/Process browser helpers (ui=true) */
  commandBrowser(agentId: string, cmdline: string, args: Record<string, any>) {
    return this.commandExecute(agentId, cmdline, args, true)
  }

  browserLs(agentId: string, path = '.') {
    const p = path || '.'
    return this.commandBrowser(agentId, `ls ${p}`, {
      command: 'ls',
      path: p,
      message: 'Task: list files',
    })
  }

  browserDisks(agentId: string) {
    return this.commandBrowser(agentId, 'disks', {
      command: 'disks',
      message: 'Task: show mounted disks',
    })
  }

  browserPsList(agentId: string) {
    return this.commandBrowser(agentId, 'ps list', {
      command: 'ps',
      subcommand: 'list',
      message: 'Task: show process list',
    })
  }

  browserPsKill(agentId: string, pid: number) {
    return this.commandBrowser(agentId, `ps kill ${pid}`, {
      command: 'ps',
      subcommand: 'kill',
      pid,
      message: 'Task: kill process',
    })
  }

  browserDownload(agentId: string, filePath: string) {
    return this.commandBrowser(agentId, `download ${filePath}`, {
      command: 'download',
      file: filePath,
      message: 'Task: download file',
    })
  }

  browserRm(agentId: string, path: string) {
    // rm is normal task (console), not browser UI packet
    return this.commandExecute(agentId, `rm ${path}`, {
      command: 'rm',
      path,
      message: 'Task: remove path',
    }, false)
  }

  browserCat(agentId: string, path: string) {
    return this.commandExecute(agentId, `cat ${path}`, {
      command: 'cat',
      path,
      message: 'Task: cat file',
    }, false)
  }

  downloadSync(fileId: string) {
    return this.post<{ ok: boolean; filename?: string; content?: string }>('/api/download/sync', { file_id: fileId })
  }

  connectWsUrl(otp: string): string {
    if (!this.baseUrl || this.baseUrl.startsWith('/')) {
      const proto = location.protocol === 'https:' ? 'wss:' : 'ws:'
      return `${proto}//${location.host}/api/connect?otp=${encodeURIComponent(otp)}`
    }
    const u = new URL(this.baseUrl)
    const proto = u.protocol === 'https:' ? 'wss:' : 'ws:'
    const path = u.pathname.replace(/\/+$/, '')
    const prefix = path.endsWith('/api') ? path : (path + '/api')
    return `${proto}//${u.host}${prefix}/connect?otp=${encodeURIComponent(otp)}`
  }
}
