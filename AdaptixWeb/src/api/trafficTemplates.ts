/** HTTP / WS 监听器流量模板。URI 需满足 beacon 校验：`/[A-Za-z0-9.=-]+(/...)*` */

export type TrafficListenerType = 'BeaconHTTP' | 'BeaconWS'

export type TrafficPatch = {
  http_method?: string
  uri: string[]
  user_agent: string[]
  hb_header: string
  request_headers: string
  server_headers: string
  'page-error': string
  'page-payload': string
}

export type TrafficTemplate = {
  id: string
  label: string
  hint: string
  types: TrafficListenerType[]
  patch: TrafficPatch
}

const IIS_404 = `<!DOCTYPE html>
<html>
<head><title>404 - File or directory not found.</title></head>
<body>
<h2>404 - File or directory not found.</h2>
<h3>The resource you are looking for might have been removed, had its name changed, or is temporarily unavailable.</h3>
</body>
</html>`

const NGINX_404 = `<html>
<head><title>404 Not Found</title></head>
<body>
<center><h1>404 Not Found</h1></center>
<hr><center>nginx</center>
</body>
</html>`

const APACHE_404 = `<!DOCTYPE HTML PUBLIC "-//IETF//DTD HTML 2.0//EN">
<html><head><title>404 Not Found</title></head>
<body>
<h1>Not Found</h1>
<p>The requested URL was not found on this server.</p>
</body></html>`

const CHROME_UA = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36'
const EDGE_UA = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36 Edg/122.0.0.0'
const FIREFOX_OLD_UA = 'Mozilla/5.0 (Windows NT 6.2; rv:20.0) Gecko/20121202 Firefox/20.0'

export const TRAFFIC_TEMPLATES: TrafficTemplate[] = [
  {
    id: 'official',
    label: '官方默认',
    hint: 'Adaptix 自带 URI / JSON 回包',
    types: ['BeaconHTTP', 'BeaconWS'],
    patch: {
      http_method: 'POST',
      uri: ['/api/v1/status', '/updates/check.php', '/content.html'],
      user_agent: [FIREFOX_OLD_UA],
      hb_header: 'X-Beacon-Id',
      request_headers: '',
      server_headers: 'Server: nginx\nContent-Type: application/json; charset=utf-8',
      'page-error': APACHE_404,
      'page-payload': '{"status": "ok", "data": "<<<PAYLOAD_DATA>>>", "metrics": "sync"}',
    },
  },
  {
    id: 'o365',
    label: 'Office 365',
    hint: 'Outlook / EWS / ActiveSync',
    types: ['BeaconHTTP'],
    patch: {
      http_method: 'POST',
      uri: [
        '/owa/service.svc',
        '/autodiscover/autodiscover.xml',
        '/Microsoft-Server-ActiveSync',
        '/ews/exchange.asmx',
      ],
      user_agent: [
        'Microsoft Office/16.0 (Windows NT 10.0; Microsoft Outlook 16.0.17328; Pro)',
        EDGE_UA,
      ],
      hb_header: 'client-request-id',
      request_headers: 'Accept: */*\nAccept-Language: zh-CN,zh;q=0.9,en;q=0.8\nX-Office-Version: 16.0.17328',
      server_headers: 'Server: Microsoft-IIS/10.0\nX-Powered-By: ASP.NET\nrequest-id: 00000000-0000-0000-0000-000000000000\nContent-Type: text/xml; charset=utf-8',
      'page-error': IIS_404,
      'page-payload': '<?xml version="1.0" encoding="utf-8"?><s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"><s:Body><SyncResponse><Data><<<PAYLOAD_DATA>>></Data></SyncResponse></s:Body></s:Envelope>',
    },
  },
  {
    id: 'wupdate',
    label: 'Windows Update',
    hint: 'WU / 投递优化回连',
    types: ['BeaconHTTP'],
    patch: {
      http_method: 'POST',
      uri: [
        '/v11/windowsupdate/redir',
        '/v11/windowsupdate/selfupdate',
        '/clientwebservice/wusserverversion.asmx',
        '/wuident.cab',
      ],
      user_agent: ['Windows-Update-Agent/10.0.22621.2506 Client-Protocol/2.31'],
      hb_header: 'MS-CV',
      request_headers: 'Accept: */*\nContent-Type: application/soap+xml; charset=utf-8',
      server_headers: 'Server: Microsoft-IIS/10.0\nX-Content-Type-Options: nosniff\nContent-Type: text/xml; charset=utf-8',
      'page-error': IIS_404,
      'page-payload': '<?xml version="1.0"?><soap:Envelope xmlns:soap="http://www.w3.org/2003/05/soap-envelope"><soap:Body><GetUpdateResponse><Payload><<<PAYLOAD_DATA>>></Payload></GetUpdateResponse></soap:Body></soap:Envelope>',
    },
  },
  {
    id: 'chrome',
    label: 'Chrome 更新',
    hint: 'Google Omaha / CRX',
    types: ['BeaconHTTP'],
    patch: {
      http_method: 'POST',
      uri: ['/service/update2/json', '/crx/blobs', '/download/chrome'],
      user_agent: [CHROME_UA],
      hb_header: 'X-Goog-Request-Id',
      request_headers: 'Accept: application/json\nAccept-Language: zh-CN,zh;q=0.9\nContent-Type: application/json',
      server_headers: 'Server: ESF\nX-Content-Type-Options: nosniff\nX-Frame-Options: SAMEORIGIN\nContent-Type: application/json; charset=utf-8',
      'page-error': NGINX_404,
      'page-payload': '{"response":{"protocol":"3.1","apps":[]},"data":"<<<PAYLOAD_DATA>>>"}',
    },
  },
  {
    id: 'wework',
    label: '企业微信',
    hint: 'cgi-bin / wework 同步',
    types: ['BeaconHTTP'],
    patch: {
      http_method: 'POST',
      uri: ['/cgi-bin/getinfo', '/cgi-bin/sync', '/wework/check'],
      user_agent: [
        'Mozilla/5.0 (Windows NT 10.0; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36 wxwork/4.1.32',
        CHROME_UA,
      ],
      hb_header: 'X-Requested-With',
      request_headers: 'Accept: application/json, text/plain, */*\nAccept-Language: zh-CN,zh;q=0.9',
      server_headers: 'Server: nginx\nContent-Type: application/json; charset=utf-8\nX-Content-Type-Options: nosniff',
      'page-error': NGINX_404,
      'page-payload': '{"errcode":0,"errmsg":"ok","data":"<<<PAYLOAD_DATA>>>"}',
    },
  },
  {
    id: 'aliyun',
    label: '阿里云 CDN',
    hint: 'OSS / CDN 刷新接口',
    types: ['BeaconHTTP'],
    patch: {
      http_method: 'POST',
      uri: ['/oss/v1/object', '/cdn/refresh', '/cdn/describe'],
      user_agent: [
        'aliyun-sdk-js/6.0.0',
        CHROME_UA,
      ],
      hb_header: 'x-oss-request-id',
      request_headers: 'Accept: application/json\nContent-Type: application/json',
      server_headers: 'Server: AliyunOSS\nContent-Type: application/json; charset=utf-8\nx-oss-request-id: 000000000000000000000000',
      'page-error': NGINX_404,
      'page-payload': '{"RequestId":"0000","Code":"OK","Data":"<<<PAYLOAD_DATA>>>"}',
    },
  },
  {
    id: 'nginx-api',
    label: 'Nginx API',
    hint: '常见 REST / 遥测 JSON',
    types: ['BeaconHTTP', 'BeaconWS'],
    patch: {
      http_method: 'POST',
      uri: ['/api/v2/telemetry', '/api/v2/events', '/healthz'],
      user_agent: [CHROME_UA, EDGE_UA],
      hb_header: 'X-Request-ID',
      request_headers: 'Accept: application/json\nAccept-Language: zh-CN,zh;q=0.9,en;q=0.8\nContent-Type: application/json',
      server_headers: 'Server: nginx\nContent-Type: application/json; charset=utf-8\nCache-Control: no-store',
      'page-error': NGINX_404,
      'page-payload': '{"status":"ok","data":"<<<PAYLOAD_DATA>>>","metrics":"sync"}',
    },
  },
  {
    id: 'cdn-js',
    label: 'CDN 静态',
    hint: '伪装 JS 资源回包',
    types: ['BeaconHTTP'],
    patch: {
      http_method: 'GET',
      uri: ['/cdn/static/app.js', '/assets/js/main.min.js', '/static/chunks/vendor.js'],
      user_agent: [CHROME_UA],
      hb_header: 'X-Request-ID',
      request_headers: 'Accept: */*\nAccept-Language: zh-CN,zh;q=0.9',
      server_headers: 'Server: cloudflare\nContent-Type: application/javascript; charset=utf-8\nCache-Control: public, max-age=60',
      'page-error': NGINX_404,
      'page-payload': '/*! build */window.__BOOT="<<<PAYLOAD_DATA>>>";',
    },
  },
  {
    id: 'ws-im',
    label: '即时通讯 WS',
    hint: '聊天 / 推送 / GraphQL',
    types: ['BeaconWS'],
    patch: {
      http_method: 'GET',
      uri: ['/ws', '/im', '/notifications', '/push', '/graphql'],
      user_agent: [CHROME_UA, EDGE_UA],
      hb_header: 'Sec-WebSocket-Protocol',
      request_headers: 'Accept: */*\nCache-Control: no-cache\nPragma: no-cache',
      server_headers: 'Server: nginx\nContent-Type: application/json; charset=utf-8',
      'page-error': NGINX_404,
      'page-payload': '{"type":"sync","channel":"presence","payload":"<<<PAYLOAD_DATA>>>"}',
    },
  },
  {
    id: 'ws-socketio',
    label: 'Socket.IO',
    hint: 'socket.io / engine.io 路径',
    types: ['BeaconWS'],
    patch: {
      http_method: 'GET',
      uri: ['/socket.io', '/engine.io', '/ws'],
      user_agent: [CHROME_UA],
      hb_header: 'Sec-WebSocket-Protocol',
      request_headers: 'Accept: */*\nOrigin: https://app.example.com',
      server_headers: 'Server: nginx\nAccess-Control-Allow-Origin: *\nContent-Type: application/json; charset=utf-8',
      'page-error': NGINX_404,
      'page-payload': '{"sid":"x","upgrades":[],"data":"<<<PAYLOAD_DATA>>>"}',
    },
  },
]

const TRAFFIC_KEYS = [
  'http_method', 'uri', 'user_agent', 'hb_header',
  'request_headers', 'server_headers', 'page-error', 'page-payload',
] as const

export function templatesFor(type: string): TrafficTemplate[] {
  return TRAFFIC_TEMPLATES.filter((t) => t.types.includes(type as TrafficListenerType))
}

export function applyTrafficTemplate(
  cfg: Record<string, any>,
  type: string,
  tpl: TrafficTemplate,
): Record<string, any> {
  const patch: Record<string, any> = tpl.id === 'official'
    ? { ...officialPatchFor(type) }
    : { ...tpl.patch }
  if (type === 'BeaconWS') {
    patch.http_method = 'GET'
    patch.protocol = 'ws'
    if (!patch.hb_header) patch.hb_header = 'Sec-WebSocket-Protocol'
  } else if (type === 'BeaconHTTP') {
    patch.protocol = 'http'
  }
  return { ...cfg, ...patch }
}

export function officialPatchFor(type: string): TrafficPatch {
  if (type === 'BeaconWS') {
    return {
      http_method: 'GET',
      uri: ['/ws', '/chat', '/socket.io', '/graphql'],
      user_agent: [FIREFOX_OLD_UA],
      hb_header: 'Sec-WebSocket-Protocol',
      request_headers: '',
      server_headers: 'Server: nginx\nContent-Type: application/json; charset=utf-8',
      'page-error': APACHE_404,
      'page-payload': '{"type":"sync","channel":"presence","payload":"<<<PAYLOAD_DATA>>>"}',
    }
  }
  return TRAFFIC_TEMPLATES[0].patch
}

export function trafficFieldKeys() {
  return TRAFFIC_KEYS
}
