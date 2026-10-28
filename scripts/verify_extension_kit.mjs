#!/usr/bin/env node
/**
 * Static readiness checks for Extension-Kit AxScripts.
 *
 * This intentionally does not execute AxScripts or BOFs. It checks the
 * repository layout and literal contracts without an agent or teamserver.
 */
import fs from 'node:fs'
import path from 'node:path'
import process from 'node:process'

const root = path.resolve(process.argv[2] || path.join(import.meta.dirname, '..', 'Extension-Kit'))
const errors = []
const warnings = []
let axsCount = 0
let importCount = 0
let packCount = 0

function fail(message) { errors.push(message) }
function warn(message) { warnings.push(message) }
function rel(file) { return path.relative(root, file).replaceAll(path.sep, '/') }

function walk(dir) {
  if (!fs.existsSync(dir)) return []
  const result = []
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const file = path.join(dir, entry.name)
    if (entry.isDirectory()) {
      if (entry.name !== '_bin' && entry.name !== '.git') result.push(...walk(file))
    } else {
      result.push(file)
    }
  }
  return result
}

function splitTopLevel(text) {
  const values = []
  let start = 0
  let quote = ''
  let escaped = false
  let round = 0
  let square = 0
  let curly = 0
  for (let i = 0; i < text.length; i += 1) {
    const ch = text[i]
    if (quote) {
      if (escaped) escaped = false
      else if (ch === '\\\\') escaped = true
      else if (ch === quote) quote = ''
      continue
    }
    if (ch === '"' || ch === "'") {
      quote = ch
      continue
    }
    if (ch === '(') round += 1
    else if (ch === ')') round -= 1
    else if (ch === '[') square += 1
    else if (ch === ']') square -= 1
    else if (ch === '{') curly += 1
    else if (ch === '}') curly -= 1
    else if (ch === ',' && round === 0 && square === 0 && curly === 0) {
      values.push(text.slice(start, i).trim())
      start = i + 1
    }
  }
  if (text.slice(start).trim()) values.push(text.slice(start).trim())
  return values
}

function extractCallBodies(source, name) {
  const calls = []
  const needle = name + '('
  let offset = 0
  while (true) {
    const start = source.indexOf(needle, offset)
    if (start < 0) break
    const open = source.indexOf('(', start)
    let depth = 0
    let quote = ''
    let escaped = false
    let end = -1
    for (let i = open; i < source.length; i += 1) {
      const ch = source[i]
      if (quote) {
        if (escaped) escaped = false
        else if (ch === '\\\\') escaped = true
        else if (ch === quote) quote = ''
        continue
      }
      if (ch === '"' || ch === "'") {
        quote = ch
        continue
      }
      if (ch === '(') depth += 1
      else if (ch === ')') {
        depth -= 1
        if (depth === 0) {
          end = i
          break
        }
      }
    }
    if (end < 0) {
      fail('unclosed call: ' + name)
      break
    }
    calls.push(source.slice(open + 1, end))
    offset = end + 1
  }
  return calls
}

function checkImports(file, source) {
  const direct = /ax\.script_import\(\s*["']([^"']+)["']\s*\)/g
  const relative = /ax\.script_import\(\s*ax\.script_dir\(\)\s*\+\s*["']([^"']+)["']\s*\)/g
  for (const match of source.matchAll(direct)) {
    const target = path.resolve(path.dirname(file), match[1])
    importCount += 1
    if (!fs.existsSync(target)) fail(rel(file) + ': missing import ' + match[1])
  }
  for (const match of source.matchAll(relative)) {
    const target = path.resolve(path.dirname(file), match[1])
    importCount += 1
    if (!fs.existsSync(target)) fail(rel(file) + ': missing import ' + match[1])
  }
}

function checkPackArity(file, source) {
  for (const body of extractCallBodies(source, 'ax.bof_pack')) {
    const args = splitTopLevel(body)
    if (args.length < 2) {
      fail(rel(file) + ': ax.bof_pack requires a format and an argument array')
      continue
    }
    const formatMatch = args[0].match(/^["']([^"']*)["']$/)
    const arrayText = args.slice(1).join(',').trim()
    if (!formatMatch || !arrayText.startsWith('[') || !arrayText.endsWith(']')) continue
    const formatCount = formatMatch[1].split(',').filter(Boolean).length
    const valueCount = splitTopLevel(arrayText.slice(1, -1)).length
    packCount += 1
    if (formatCount !== valueCount) {
      fail(rel(file) + ': ax.bof_pack count mismatch, format=' + formatCount + ' values=' + valueCount)
    }
  }
}

function checkMimikatzLayout() {
  const pe = path.join(root, 'Creds-BOF', 'mimikatz', '_bin', 'mimikatz.x64.exe')
  const loader = path.join(root, 'Execution-BOF', '_bin', 'NoConsolation.x64.o')
  if (!fs.existsSync(pe)) warn('Creds-BOF/mimikatz/_bin/mimikatz.x64.exe is missing; run bash Creds-BOF/mimikatz/fetch_mimikatz.sh')
  if (!fs.existsSync(loader)) warn('Execution-BOF/_bin/NoConsolation.x64.o is missing; run make -C Execution-BOF')
}

if (!fs.existsSync(root)) {
  fail('Extension-Kit directory not found: ' + root)
} else {
  for (const file of walk(root)) {
    if (!file.endsWith('.axs')) continue
    axsCount += 1
    const source = fs.readFileSync(file, 'utf8')
    checkImports(file, source)
    checkPackArity(file, source)
  }
  checkMimikatzLayout()
}

for (const message of warnings) console.log('WARN ' + message)
for (const message of errors) console.error('ERR  ' + message)
console.log('Checked ' + axsCount + ' AxScripts, ' + importCount + ' literal imports, ' + packCount + ' literal bof_pack calls.')
if (errors.length) {
  console.error('Extension-Kit validation failed: ' + errors.length + ' error(s).')
  process.exitCode = 1
} else {
  console.log('Extension-Kit validation passed.')
}

