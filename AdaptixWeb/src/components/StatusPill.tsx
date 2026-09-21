/** Connection / entity status */

export function ConnStatus({
  connected,
  wsConnected,
  syncing,
}: {
  connected: boolean
  wsConnected: boolean
  syncing: boolean
}) {
  if (!connected) {
    return (
      <span className="conn-chip is-off" title="未登录或未连接服务器">
        <span className="conn-dot" aria-hidden />
        <span className="conn-label">
          <span className="conn-title">离线</span>
          <span className="conn-sub">未连接</span>
        </span>
      </span>
    )
  }
  if (syncing) {
    return (
      <span className="conn-chip is-sync" title="正在同步数据">
        <span className="conn-dot" aria-hidden />
        <span className="conn-label">
          <span className="conn-title">同步</span>
          <span className="conn-sub">拉取中</span>
        </span>
      </span>
    )
  }
  if (wsConnected) {
    return (
      <span className="conn-chip is-live" title="REST + WebSocket 实时通道已连接">
        <span className="conn-dot" aria-hidden />
        <span className="conn-label">
          <span className="conn-title">实时</span>
          <span className="conn-sub">WebSocket</span>
        </span>
      </span>
    )
  }
  return (
    <span className="conn-chip is-rest" title="仅 REST，实时通道未连接（约 15s 轮询）">
      <span className="conn-dot" aria-hidden />
      <span className="conn-label">
        <span className="conn-title">轮询</span>
        <span className="conn-sub">REST</span>
      </span>
    </span>
  )
}
