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
    ws.onopen = () => handlers.onOpen?.()
    ws.onclose = (ev) => handlers.onClose?.(ev)
    ws.onerror = (ev) => handlers.onError?.(ev)
    ws.onmessage = (ev) => {
      try {
        let text = ''
        if (typeof ev.data === 'string') text = ev.data
        else if (ev.data instanceof ArrayBuffer) text = new TextDecoder().decode(ev.data)
        else return
        for (const c of text.split('\n').map((s) => s.trim()).filter(Boolean)) {
          handlers.onPacket?.(JSON.parse(c) as Packet)
        }
      } catch (e) {
        console.warn('ws parse error', e)
      }
    }
  }

  close() {
    if (this.ws) {
      try { this.ws.close() } catch { /* ignore */ }
      this.ws = null
    }
  }
}
