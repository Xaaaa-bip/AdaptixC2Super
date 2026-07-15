import React from 'react'

type Props = { children: React.ReactNode }
type State = { error: Error | null }

export class ErrorBoundary extends React.Component<Props, State> {
  state: State = { error: null }
  static getDerivedStateFromError(error: Error) {
    return { error }
  }
  componentDidCatch(error: Error, info: any) {
    console.error('UI crash', error, info)
  }
  render() {
    if (this.state.error) {
      return (
        <div style={{ padding: 24, fontFamily: 'sans-serif', color: '#b91c1c', background: '#fff1f0', height: '100%' }}>
          <h2 style={{ marginTop: 0 }}>页面渲染出错</h2>
          <pre style={{ whiteSpace: 'pre-wrap' }}>{String(this.state.error?.stack || this.state.error)}</pre>
          <button onClick={() => location.reload()}>刷新页面</button>
        </div>
      )
    }
    return this.props.children
  }
}
