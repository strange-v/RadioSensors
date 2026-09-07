import { execFileSync } from 'node:child_process'
import { mkdirSync, writeFileSync } from 'node:fs'
import { fileURLToPath, URL } from 'node:url'
import vue from '@vitejs/plugin-vue'
import { defineConfig, type Plugin } from 'vite'
import packageJson from './package.json'

const outputDirectory = fileURLToPath(new URL('../data', import.meta.url))
const repositoryDirectory = fileURLToPath(new URL('../../..', import.meta.url))
const gatewayTarget = process.env.GATEWAY_URL?.trim() || 'http://rf-gateway-a085e3e6cc20'
const gatewayProxy = { target: gatewayTarget, changeOrigin: true }

function gitSha(): string {
  const configured = process.env.GIT_SHA?.trim()
  if (configured) return configured
  try {
    return execFileSync('git', ['-c', 'safe.directory=*', 'rev-parse', '--short=12', 'HEAD'], { cwd: repositoryDirectory, encoding: 'utf8' }).trim()
  } catch {
    throw new Error('Unable to determine Git SHA. Set GIT_SHA for production builds.')
  }
}

function manifestPlugin(): Plugin {
  return {
    name: 'gateway-ui-manifest',
    closeBundle() {
      mkdirSync(outputDirectory, { recursive: true })
      writeFileSync(`${outputDirectory}/ui-manifest.json`, `${JSON.stringify({ ui_version: packageJson.version, api_version: 1, build: gitSha() })}\n`)
    },
  }
}

export default defineConfig({
  plugins: [vue(), manifestPlugin()],
  build: {
    outDir: outputDirectory,
    emptyOutDir: true,
    assetsDir: 'assets',
    sourcemap: false,
    // One CSS file, and one application JS file: every extra file costs an HTTP
    // round trip to the ESP32 plus a whole 4 KB LittleFS block, so a view only
    // earns a chunk once it is both sizeable and rarely opened. Most views are
    // neither, and are imported eagerly in src/router/index.ts.
    //
    // Two are: the pairing QR scanner, whose jsQR dependency is ~48 KB gzip
    // that only people who scan a label download, and the administration page
    // (~4.8 KB gzip of users, API tokens and their dialogs), which a viewer
    // cannot even reach. check-build.mjs budgets these apart from `app`.
    cssCodeSplit: false,
    rollupOptions: { output: { entryFileNames: 'assets/app-[hash].js', chunkFileNames: 'assets/[name]-[hash].js' } },
  },
  server: { proxy: { '/api': gatewayProxy, '/health': gatewayProxy } },
  define: { __UI_VERSION__: JSON.stringify(packageJson.version) },
})
