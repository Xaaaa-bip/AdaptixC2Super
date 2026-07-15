import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

// On teamserver: proxy /api to local adaptixserver TLS.
const target = process.env.VITE_API_TARGET || 'https://127.0.0.1:8080'

export default defineConfig({
  plugins: [react()],
  server: {
    host: '0.0.0.0',
    port: 5173,
    proxy: {
      '/api': {
        target,
        changeOrigin: true,
        secure: false,
        ws: true,
      },
      // VNC public routes are under teamserver endpoint prefix /api already;
      // keep explicit aliases if viewer is opened absolute against Vite origin.
      '/vnc': {
        target,
        changeOrigin: true,
        secure: false,
        rewrite: (p) => '/api' + p,
      },
    },
  },
  preview: {
    host: '0.0.0.0',
    port: 5173,
  },
  build: {
    outDir: 'dist',
    sourcemap: false,
  },
})
