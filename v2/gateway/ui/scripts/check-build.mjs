import { readdirSync, readFileSync, statSync } from 'node:fs'
import { join, resolve } from 'node:path'

const output = resolve(import.meta.dirname, '../../data')
const limits = { js: 80 * 1024, css: 20 * 1024, total: 250 * 1024 }
const files = readdirSync(output, { recursive: true }).map((name) => join(output, name)).filter((name) => statSync(name).isFile())
let total = 0
const compressed = { js: 0, css: 0 }
for (const file of files) {
  const bytes = readFileSync(file)
  total += bytes.length
  if (file.endsWith('.js.gz')) compressed.js += bytes.length
  if (file.endsWith('.css.gz')) compressed.css += bytes.length
}
const failures = []
for (const kind of ['js', 'css']) if (compressed[kind] > limits[kind]) failures.push(`${kind} gzip ${compressed[kind]} B exceeds ${limits[kind]} B`)
if (total > limits.total) failures.push(`total ${total} B exceeds ${limits.total} B`)
if (files.some((file) => file.endsWith('.js') || file.endsWith('.css'))) failures.push('uncompressed JS or CSS remains in the production image')
if (!files.some((file) => file.endsWith('.js.gz'))) failures.push('production image contains no compressed JavaScript')
console.log(`Bundle: JS ${compressed.js} B gzip, CSS ${compressed.css} B gzip, LittleFS total ${total} B`)
if (failures.length) throw new Error(`Bundle budget exceeded: ${failures.join('; ')}`)
