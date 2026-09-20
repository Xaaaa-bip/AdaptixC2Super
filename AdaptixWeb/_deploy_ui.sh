#!/bin/bash
set -e
cd /root/AdaptixC2/AdaptixWeb
pkill -f 'vite --host 0.0.0.0 --port 5173' 2>/dev/null || true
sleep 1
rm -rf node_modules/.vite
nohup npm run dev > /tmp/adaptixweb-vite.log 2>&1 &
sleep 3
pgrep -af 'vite --host' | head -2 || true
python3 - <<'PY'
import urllib.request
urls = [
  'http://127.0.0.1:5173/',
  'http://127.0.0.1:5173/src/App.tsx',
  'http://127.0.0.1:5173/src/components/Login.tsx',
  'http://127.0.0.1:5173/src/components/Dashboard.tsx',
  'http://127.0.0.1:5173/src/components/Hosts.tsx',
  'http://127.0.0.1:5173/src/styles.css',
  'http://127.0.0.1:5173/src/design/tokens.css',
  'http://127.0.0.1:5173/src/design/ops-hard.css',
]
for u in urls:
    r = urllib.request.urlopen(u, timeout=10)
    b = r.read()
    print(r.status, len(b), 'ZERO' if len(b)==0 else 'ok', u.split('/')[-1] or 'root')
PY
tail -n 12 /tmp/adaptixweb-vite.log | tr -cd '\11\12\15\40-\176'
