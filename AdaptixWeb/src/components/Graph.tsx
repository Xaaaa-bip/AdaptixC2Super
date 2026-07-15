import { useMemo, useState } from 'react'
import { useStore } from '../store'
import { isAgentOnline } from '../utils'

type Node = {
  id: string
  label: string
  sub: string
  live: boolean
  x: number
  y: number
}

export function Graph() {
  const agents = useStore((s) => s.agents) || {}
  const pivots = useStore((s) => s.pivots) || {}
  const selectAgent = useStore((s) => s.selectAgent)
  const openConsole = useStore((s) => s.openConsole)
  const setTab = useStore((s) => s.setTab)
  const openUiTab = useStore((s) => s.openUiTab)
  const [q, setQ] = useState('')

  const agentList = useMemo(() => {
    let arr = Object.values(agents)
    if (q.trim()) {
      const s = q.trim().toLowerCase()
      arr = arr.filter((a) =>
        [a.a_id, a.a_computer, a.a_username, a.a_internal_ip, a.a_listener]
          .filter(Boolean).some((x) => String(x).toLowerCase().includes(s)))
    }
    return arr
  }, [agents, q])

  const pivotList = useMemo(() => Object.values(pivots), [pivots])

  // layout: roots (no parent) on left rings; children follow parents when pivot known
  const parentOf = useMemo(() => {
    const m = new Map<string, string>()
    for (const p of pivotList) {
      if (p.p_child_agent_id && p.p_parent_agent_id) {
        m.set(p.p_child_agent_id, p.p_parent_agent_id)
      }
    }
    return m
  }, [pivotList])

  const nodes: Node[] = useMemo(() => {
    const W = 920
    const H = 520
    const roots = agentList.filter((a) => !parentOf.has(a.a_id))
    const children = agentList.filter((a) => parentOf.has(a.a_id))
    const out: Node[] = []
    const nR = Math.max(roots.length, 1)
    roots.forEach((a, i) => {
      const angle = (Math.PI * 1.2 * i) / nR - Math.PI * 0.1
      const r = 160
      out.push({
        id: a.a_id,
        label: a.a_computer || a.a_id.slice(0, 8),
        sub: `${a.a_internal_ip || a.a_external_ip || '-'} · ${a.a_listener || '-'}`,
        live: isAgentOnline(a),
        x: 220 + Math.cos(angle) * r,
        y: H / 2 + Math.sin(angle) * r * 0.7,
      })
    })
    // children near parent
    const byId = new Map(out.map((n) => [n.id, n]))
    children.forEach((a, i) => {
      const pid = parentOf.get(a.a_id)!
      const p = byId.get(pid)
      const baseX = p ? p.x + 180 : 700
      const baseY = p ? p.y + ((i % 5) - 2) * 48 : 80 + i * 50
      const n: Node = {
        id: a.a_id,
        label: a.a_computer || a.a_id.slice(0, 8),
        sub: `${a.a_internal_ip || '-'} · pivot`,
        live: isAgentOnline(a),
        x: Math.min(W - 40, baseX),
        y: Math.max(40, Math.min(H - 40, baseY)),
      }
      out.push(n)
      byId.set(n.id, n)
    })
    // if no agents
    if (!out.length) {
      out.push({ id: '_empty', label: '无主机', sub: '上线后显示拓扑', live: false, x: W / 2, y: H / 2 })
    }
    return out
  }, [agentList, parentOf])

  const edges = useMemo(() => {
    const byId = new Map(nodes.map((n) => [n.id, n]))
    const list: Array<{ from: Node; to: Node; name?: string }> = []
    for (const p of pivotList) {
      const a = byId.get(p.p_parent_agent_id || '')
      const b = byId.get(p.p_child_agent_id || '')
      if (a && b) list.push({ from: a, to: b, name: p.p_pivot_name })
    }
    return list
  }, [nodes, pivotList])

  function openAgent(id: string) {
    if (id === '_empty') return
    selectAgent(id)
    openConsole(id)
    setTab('sessions')
    openUiTab('sessions')
  }

  const W = 960
  const H = 540

  return (
    <div className="page graph-page">
      <div className="toolbar">
        <strong>拓扑图</strong>
        <input type="search" placeholder="过滤主机" value={q} onChange={(e) => setQ(e.target.value)} style={{ maxWidth: 220 }} />
        <div className="stats">
          <span>{agentList.length} 主机</span>
          <span className="dim">{pivotList.length} pivot 边</span>
        </div>
      </div>
      <div className="graph-wrap">
        <svg viewBox={`0 0 ${W} ${H}`} className="graph-svg" role="img">
          <defs>
            <marker id="arrow" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse">
              <path d="M 0 0 L 10 5 L 0 10 z" fill="currentColor" />
            </marker>
          </defs>
          {/* edges */}
          {edges.map((e, i) => (
            <g key={i} className="graph-edge">
              <line
                x1={e.from.x}
                y1={e.from.y}
                x2={e.to.x}
                y2={e.to.y}
                markerEnd="url(#arrow)"
              />
              {e.name ? (
                <text x={(e.from.x + e.to.x) / 2} y={(e.from.y + e.to.y) / 2 - 6} textAnchor="middle" className="graph-edge-label">
                  {e.name}
                </text>
              ) : null}
            </g>
          ))}
          {/* nodes */}
          {nodes.map((n) => (
            <g
              key={n.id}
              className={`graph-node ${n.live ? 'live' : 'dead'}`}
              transform={`translate(${n.x},${n.y})`}
              onClick={() => openAgent(n.id)}
              style={{ cursor: n.id === '_empty' ? 'default' : 'pointer' }}
            >
              <circle r="22" />
              <circle r="6" className="graph-dot" />
              <text y="38" textAnchor="middle" className="graph-label">{n.label}</text>
              <text y="52" textAnchor="middle" className="graph-sub">{n.sub}</text>
            </g>
          ))}
        </svg>
        {pivotList.length === 0 && agentList.length > 0 && (
          <div className="graph-hint dim">
            当前无 pivot 边：仅展示主机节点。父子链路上线后会出现连线（sync TYPE_PIVOT_CREATE）。
          </div>
        )}
      </div>
    </div>
  )
}
