#!/usr/bin/env python3
from pathlib import Path
import urllib.request

base = Path('/root/AdaptixC2/AdaptixWeb')
for p in ['src/styles.css', 'src/App.tsx', 'src/components/Login.tsx', 'src/main.tsx', 'src/components/Dashboard.tsx']:
    b = (base / p).read_bytes()
    print(f'{p}: size={len(b)} head={b[:4]!r} bom={b.startswith(b"\xef\xbb\xbf")}')

urls = [
    'http://127.0.0.1:5173/',
    'http://127.0.0.1:5173/src/main.tsx',
    'http://127.0.0.1:5173/src/App.tsx',
    'http://127.0.0.1:5173/src/components/Login.tsx',
    'http://127.0.0.1:5173/src/components/Dashboard.tsx',
    'http://127.0.0.1:5173/src/styles.css',
    'http://127.0.0.1:5173/src/design/tokens.css',
]
for u in urls:
    try:
        with urllib.request.urlopen(u, timeout=10) as r:
            body = r.read()
            text = body.decode('utf-8', 'replace')
            bad = any(x in text for x in ('Internal server error', 'Pre-transform error', 'Failed to parse', 'SyntaxError'))
            print(f'{r.status} {len(body):6d} bad={bad} {u}')
            if bad:
                print(text[:800])
                print('---')
    except Exception as e:
        print(f'ERR {u}: {e}')

# Check for invalid font-weight 750 (some engines ok, not a blank cause)
css = (base / 'src/styles.css').read_text('utf-8', 'replace')
print('font-weight:750 count', css.count('font-weight: 750'))
print('unbalanced braces', css.count('{') - css.count('}'))
