#!/bin/bash
set -e
cd /root/AdaptixC2/AdaptixWeb
pkill -f 'vite --host 0.0.0.0 --port 5173' 2>/dev/null || true
sleep 1
rm -rf node_modules/.vite
nohup npm run dev > /tmp/adaptixweb-vite.log 2>&1 &
sleep 4
pgrep -af 'vite --host' || true
python3 /tmp/diag_blank.py
echo '--- log ---'
tail -n 25 /tmp/adaptixweb-vite.log | tr -cd '\11\12\15\40-\176' | tail -n 25
