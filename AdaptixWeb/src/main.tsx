import React from 'react'
import ReactDOM from 'react-dom/client'
import App from './App'
import { ErrorBoundary } from './components/ErrorBoundary'
import { loadDensity } from './utils'
import { applyQtSettings, loadQtSettings } from './design/qt-themes'
import { LANG } from './i18n'
import { installEnDomTranslator } from './i18n/domTranslate'
import './styles.css'
import './design/studio.css'

// Apply full Qlementine + Console theme & font preferences before first paint
applyQtSettings(loadQtSettings())
document.documentElement.setAttribute('data-density', loadDensity())

ReactDOM.createRoot(document.getElementById('root')!).render(
  <React.StrictMode>
    <ErrorBoundary>
      <App />
    </ErrorBoundary>
  </React.StrictMode>,
)

if (LANG === 'en') {
  installEnDomTranslator()
}
