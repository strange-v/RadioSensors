import { gzipSync } from 'node:zlib'
import { readFileSync, readdirSync, unlinkSync, writeFileSync } from 'node:fs'
import { extname, join, resolve } from 'node:path'

const output = resolve(import.meta.dirname, '../../data')
const assets = readdirSync(output, { recursive: true })
  .map((name) => join(output, name))
  .filter((name) => ['.js', '.css', '.html'].includes(extname(name)))

for (const asset of assets) {
  const compressed = gzipSync(readFileSync(asset), { level: 9, mtime: 0 })
  writeFileSync(`${asset}.gz`, compressed)
  unlinkSync(asset)
}

console.log(`Compressed ${assets.length} static assets for LittleFS`)
