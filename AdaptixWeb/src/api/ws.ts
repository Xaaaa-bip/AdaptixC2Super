import type { Packet } from './types'

export type WsHandlers = {
  onOpen?: () => void
  onClose?: (ev: CloseEvent) => void
  onError?: (ev: Event) => void
  onPacket?: (pkt: Packet) => void
}

export class SyncSocket {
  private ws: WebSocket | null = null

  connect(url: string, handlers: WsHandlers) {
    this.close()
    const ws = new WebSocket(url)
    ws.binaryType = 'arraybuffer'
    this.ws = ws
    ws.onopen = () => {
      if (this.ws !== ws) return
      handlers.onOpen?.()
    }
    ws.onclose = (ev) => {
      if (this.ws !== ws) return
      this.ws = null
      handlers.onClose?.(ev)
    }
    ws.onerror = (ev) => {
      if (this.ws !== ws) return
      handlers.onError?.(ev)
    }
    ws.onmessage = (ev) => {
      if (this.ws !== ws) return
      try {
        let text = ''
        if (typeof ev.data === 'string') text = ev.data
        else if (ev.data instanceof ArrayBuffer) text = new TextDecoder().decode(ev.data)
        else return
        for (const c of text.split('\n').map((s) => s.trim()).filter(Boolean)) {
          if (this.ws !== ws) return
          handlers.onPacket?.(JSON.parse(c) as Packet)
        }
      } catch (e) {
        console.warn('ws parse error', e)
      }
    }
  }

  close() {
    const ws = this.ws
    if (ws) {
      this.ws = null
      ws.onopen = null
      ws.onclose = null
      ws.onerror = null
      ws.onmessage = null
      try { ws.close() } catch { /* ignore */ }
    }
  }
}
