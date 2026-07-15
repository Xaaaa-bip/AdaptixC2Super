import React from 'react'
import ReactDOM from 'react-dom/client'
import App from './App'
import { ErrorBoundary } from './components/ErrorBoundary'
import { loadDensity, loadTheme } from './utils'
import './styles.css'

// Theme / density defaults from localStorage
document.documentElement.setAttribute('data-theme', loadTheme())
document.documentElement.setAttribute('data-density', loadDensity())

ReactDOM.createRoot(document.getElementById('root')!).render(
  <React.StrictMode>
    <ErrorBoundary>
      <App />
    </ErrorBoundary>
  </React.StrictMode>,
)
