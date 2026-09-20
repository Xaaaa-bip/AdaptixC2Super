/** Parse beacon cleartext tables for file/process browsers */

export type FileItem = {
  name: string
  isDir: boolean
  size: number
  date: number
  dateText?: string
  sizeText?: string
}

export type ProcItem = {
  pid: number
  ppid: number
  session?: number
  arch?: string
  context?: string
  name: string
}

/**
 * Parse `ls` cleartext table from beacon COMMAND_LS:
 *  Type     Size           Last Modified        Name
 *  ----     ---------      ----------------     ----
 *  dir                     10/07/2026 06:43     Windows
 *           1.23 Kb        10/07/2026 06:43     readme.txt
 *
 * Size may contain a space ("1.23 Kb") — do NOT use single \S+ for size.
 */
export function parseLsText(text: string): FileItem[] {
  const items: FileItem[] = []
  for (const raw of (text || '').split(/\r?\n/)) {
    const line = raw.replace(/\s+$/, '')
    if (!line.trim()) continue
    if (/^\s*Type\b/i.test(line)) continue
    if (/^\s*-{2,}/.test(line.trim())) continue
    if (/^\s*----\b/.test(line.trim())) continue

    // dir: optional leading spaces + "dir" + blank size + date + name
    const mDir = line.match(/^\s*dir\s+(\d{1,2}\/\d{1,2}\/\d{4}\s+\d{1,2}:\d{2})\s+(.+?)\s*$/i)
    if (mDir) {
      const name = mDir[2].trim()
      if (name && name !== '.' && name !== '..') {
        items.push({ name, isDir: true, size: 0, date: 0, dateText: mDir[1] })
      }
      continue
    }

    // file: size (may include unit with space) + date + name
    // Examples:
    //   "  12.34 Kb       01/02/2026 12:00  foo.txt"
    //   "  1024           01/02/2026 12:00  bare"
    //   "  1.5 Mb         01/02/2026 12:00  a b c.dat"
    const mFile = line.match(
      /^\s*(\d+(?:\.\d+)?(?:\s*[KkMmGg][Bb]?)?)\s+(\d{1,2}\/\d{1,2}\/\d{4}\s+\d{1,2}:\d{2})\s+(.+?)\s*$/,
    )
    if (mFile) {
      const name = mFile[3].trim()
      if (name && name !== '.' && name !== '..' && !/^dir$/i.test(mFile[1])) {
        items.push({
          name,
          isDir: false,
          size: parseSizeToBytes(mFile[1]),
          date: 0,
          sizeText: mFile[1].replace(/\s+/g, ' ').trim(),
          dateText: mFile[2],
        })
      }
      continue
    }

    // fallback: trailing date + name (rare layouts)
    const mLoose = line.match(/(\d{1,2}\/\d{1,2}\/\d{4}\s+\d{1,2}:\d{2})\s+(.+?)\s*$/)
    if (mLoose) {
      const name = mLoose[2].trim()
      if (name && name !== '.' && name !== '..' && !/^(Type|Name|----)/i.test(name)) {
        const isDir = /^\s*dir\b/i.test(line)
        items.push({ name, isDir, size: 0, date: 0, dateText: mLoose[1] })
      }
    }
  }
  return items
}

function parseSizeToBytes(s: string): number {
  const t = String(s || '').trim().replace(/\s+/g, '')
  const m = t.match(/^(\d+(?:\.\d+)?)([KkMmGg][Bb]?)?$/)
  if (!m) return 0
  const n = Number(m[1])
  if (!Number.isFinite(n)) return 0
  const u = (m[2] || '').toLowerCase()
  if (u.startsWith('g')) return Math.round(n * 1024 * 1024 * 1024)
  if (u.startsWith('m')) return Math.round(n * 1024 * 1024)
  if (u.startsWith('k')) return Math.round(n * 1024)
  return Math.round(n)
}

/** Parse path from message like: Listing 'C:\'  / Listing 'D:\download' */
export function parseListingPath(message: string, fallback = '.'): string {
  const m = (message || '').match(/Listing\s+'([^']*)'/i)
  if (!m) return fallback
  return m[1] || fallback
}

/**
 * Parse `ps list` cleartext tree table:
 *  PID    PPID   Session   Arch   Context   Process
 *  ---    ----   -------   ----   -------   -------
 *  4      0      0         x64    NT AUTH…  └─ System
 */
export function parsePsText(text: string): ProcItem[] {
  const out: ProcItem[] = []
  const seen = new Set<number>()
  for (const raw of (text || '').split(/\r?\n/)) {
    const line = raw.replace(/\s+$/, '')
    if (!line.trim()) continue
    if (/^\s*PID\b/i.test(line)) continue
    if (/^\s*-{2,}/.test(line.trim()) || /^\s*---\b/.test(line.trim())) continue

    // Primary: leading PID PPID Session, then free-form rest ending with process name
    // Tree glyphs (├─ └─ │) may appear before process name.
    const m = line.match(/^\s*(\d+)\s+(\d+)\s+(\d+)\s+(.*)$/)
    if (m) {
      const pid = Number(m[1])
      const ppid = Number(m[2])
      const session = Number(m[3])
      let rest = (m[4] || '').trim()
      if (!rest) continue

      // strip tree prefix from the process field (last token cluster)
      // rest examples:
      //   "x64   NT AUTHORITY\\SYSTEM   └─ svchost.exe"
      //   "      user   ├─ foo.exe"
      //   "x86      chrome.exe"
      let arch: string | undefined
      let context: string | undefined
      let name = rest

      // pull arch if first token looks like x64/x86/arm
      const archM = rest.match(/^(x64|x86|arm64|arm|wow64)\b\s*(.*)$/i)
      if (archM) {
        arch = archM[1]
        rest = archM[2].trim()
      }

      // process name is after last tree glyph or last multi-space split
      const treeIdx = Math.max(rest.lastIndexOf('├'), rest.lastIndexOf('└'), rest.lastIndexOf('│'))
      if (treeIdx >= 0) {
        const before = rest.slice(0, treeIdx).trim()
        name = rest.slice(treeIdx).replace(/^[│├└─\s]+/, '').trim()
        if (before) context = before
      } else {
        // split on 2+ spaces: context ... name
        const parts = rest.split(/\s{2,}/).map((x) => x.trim()).filter(Boolean)
        if (parts.length >= 2) {
          name = parts[parts.length - 1]
          context = parts.slice(0, -1).join(' ')
        } else {
          name = rest
        }
      }

      name = name.replace(/^[│├└─\s]+/, '').trim()
      if (!pid || !name) continue
      if (seen.has(pid)) continue
      seen.add(pid)
      out.push({
        pid,
        ppid,
        session: Number.isFinite(session) ? session : undefined,
        arch,
        context: context || undefined,
        name,
      })
      continue
    }

    // looser: PID PPID ... name
    const m2 = line.match(/^\s*(\d+)\s+(\d+)\s+.*?(\S+\.exe|\S+)$/i)
    if (m2) {
      const pid = Number(m2[1])
      if (!seen.has(pid)) {
        seen.add(pid)
        out.push({
          pid,
          ppid: Number(m2[2]),
          name: m2[3].replace(/^[│├└─\s]+/, '').trim(),
        })
      }
    }
  }
  return out.filter((p) => p.pid && p.name)
}

/** Absolute Windows/Unix path (not a bare relative name). */
export function isAbsPath(p: string) {
  const s = String(p || '').trim()
  if (!s) return false
  if (s.startsWith('\\\\')) return true // UNC
  if (/^[a-zA-Z]:[\\/]?/.test(s)) return true // C: or C:\...
  if (s.startsWith('/')) return true
  return false
}

/** Normalize Windows drive roots to `C:\` and collapse duplicate separators. */
export function normalizePath(p: string) {
  let s = String(p || '').trim()
  if (!s || s === '.') return s || '.'
  // already unix root
  if (s === '/') return '/'
  // Windows drive root C: / C:\ / C:/
  const driveOnly = s.match(/^([a-zA-Z]):[\\/]*$/)
  if (driveOnly) return driveOnly[1].toUpperCase() + ':\\'
  // Prefer backslash for Windows-looking paths
  if (/^[a-zA-Z]:/.test(s) || s.includes('\\')) {
    s = s.replace(/\//g, '\\')
    // collapse duplicate backslashes but keep UNC prefix \\
    if (s.startsWith('\\\\')) {
      s = '\\\\' + s.slice(2).replace(/\\+/g, '\\')
    } else {
      s = s.replace(/\\+/g, '\\')
    }
    // fix doubled drive: C:\C:\foo → C:\foo
    s = s.replace(/^([a-zA-Z]:\\)\1+/i, '$1')
    s = s.replace(/^([a-zA-Z]:\\)([a-zA-Z]:\\)/i, '$2')
    return s
  }
  // unix: collapse //
  return s.replace(/\/+/g, '/')
}

/**
 * Join directory + entry name safely.
 * - If `name` is already absolute, return normalized name (never prefix base).
 * - If `name` already starts with base, don't double-join.
 * - Drive root `C:` / `C:\` + `foo.txt` → `C:\foo.txt`
 */
export function joinPath(base: string, name: string) {
  const n = String(name || '').trim()
  if (!n) return normalizePath(base || '.')
  // Absolute child wins (prevents C:\ + C:\foo → C:\C:\foo)
  if (isAbsPath(n)) return normalizePath(n)

  const b = normalizePath(base || '.')
  if (!b || b === '.') return n

  // name already under base (case-insensitive for Windows)
  const bWin = /\\/.test(b) || /^[a-zA-Z]:/.test(b)
  if (bWin) {
    const bl = b.replace(/\\+$/, '').toLowerCase()
    const nl = n.replace(/\//g, '\\').toLowerCase()
    if (nl === bl || nl.startsWith(bl + '\\')) return normalizePath(n)
    if (b.endsWith('\\')) return normalizePath(b + n)
    return normalizePath(b + '\\' + n)
  }

  if (n.startsWith(b.replace(/\/+$/, '') + '/') || n === b) return normalizePath(n)
  if (b.endsWith('/')) return normalizePath(b + n)
  return normalizePath(b + '/' + n)
}

export function parentPath(path: string) {
  const p = normalizePath(path || '.')
  if (!p || p === '.' || p === '/') return p || '.'
  if (/^[a-zA-Z]:\\?$/.test(p)) return p.endsWith('\\') ? p : p + '\\'
  const norm = p.replace(/[\\/]+$/, '')
  const idx = Math.max(norm.lastIndexOf('\\'), norm.lastIndexOf('/'))
  if (idx <= 0) return norm
  // C:\foo → C:\
  if (/^[a-zA-Z]:$/.test(norm.slice(0, idx))) return norm.slice(0, idx + 1)
  return norm.slice(0, idx) || '.'
}
