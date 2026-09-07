// Throwaway dev config used only to preview styling without a real gateway.
// Serves canned /api and /health responses. Not part of the build.
import vue from '@vitejs/plugin-vue'
import { defineConfig, type Plugin } from 'vite'
import packageJson from './package.json'

const now = Date.now()

const health = {
  status: 'ok', firmware: '2.1.0', api_version: 1, board: 'esp32-poe', hostname: 'rf-gateway-a085e3',
  gateway_id: 'a1b2c3d4e5f60718', boot_id: '0011223344556677',
  reset_reason: 'power_on', uptime_ms: 191_400_000, free_heap: 184_320,
  registry: { records: 6, generation: 12 },
  setup: { required: false, active: false, remaining_seconds: 0 },
  pairing: { active: false, remaining_seconds: 0, indication: 'idle' },
  storage: { ready: true, settings_generation: 4, auth_generation: 2, secrets_generation: 1 },
  ethernet: { state: 'connected', has_ip: true, ip: '192.168.1.42', mac: '50:ff:20:2e:fd:fe' },
  ota: { enabled: true, state: 'idle', progress: 0 },
  web_ui: { state: 'ok', version: '0.1.0', required_api_version: 1 },
  telemetry: { nodes_seen: 5, updates: 14_207, last_node_id: 3, last_received_at_ms: now - 40_000 },
  time: { state: 'synchronized', unix_ms: now, last_sync_ms: now - 1_800_000 },
  websocket: { clients: 1, connections: 9, messages_sent: 2_140, messages_dropped: 0 },
  radio: {
    state: 'ready', present: true, version: 36, frequency_band_mhz: '868', frequency_hz: 868_000_000,
    bit_rate: 55_555, node_id: 1, network_id: 172, variant: 'RFM69HCW', configured_power_dbm: 14,
    encryption_enabled: true, profile: 'balanced', spi_host: 'VSPI', pins: {}, counters: {},
    last_packet: { at_ms: now - 40_000, sender_id: 3, rssi: -74 },
  },
}

const nodes = [
  { node_id: 2, device_uid: 'A1B2C3D4E5F60718293A', display_name: 'Кухня', profile_id: 1, firmware: '2.1.0', state: 'active', last_seen_at_ms: now - 120_000, rssi: -68, has_telemetry: true },
  { node_id: 3, device_uid: '0F1E2D3C4B5A69788796', display_name: 'Гараж', profile_id: 1, firmware: '2.1.0', state: 'active', last_seen_at_ms: now - 40_000, rssi: -74, has_telemetry: true },
  { node_id: 4, device_uid: '112233445566778899AA', display_name: '', profile_id: 2, firmware: '2.0.4', state: 'pending', last_seen_at_ms: now - 5_400_000, rssi: -91, has_telemetry: false },
  { node_id: 5, device_uid: 'BBCCDDEEFF0011223344', display_name: 'Тепличка', profile_id: 1, firmware: '2.1.0', state: 'active', last_seen_at_ms: now - 300_000, rssi: -59, has_telemetry: true },
]

const routes: Record<string, unknown> = {
  '/health': health,
  '/api/v1/health': health,
  '/api/v1/info': { firmware_version: '2.1.0', api_version: 1, display_name: 'RadioSensors Gateway', ui: { state: 'ok', version: '0.1.0', required_api_version: 1 }, board: 'esp32-poe', hostname: 'rf-gateway-a085e3' },
  '/api/v1/nodes': { registry_generation: 12, nodes },
  '/api/v1/setup': { setup_required: false, physical_window_active: false, remaining_seconds: 0 },
  '/api/v1/session': { user: { id: 1, username: 'admin', role: 'admin' }, csrf_token: 'mock-csrf' },
  '/api/v1/settings': { generation: 4, display_name: 'RadioSensors Gateway', mdns_enabled: true, ntp_enabled: true, pairing_window_seconds: 120, setup_window_seconds: 300, ntp_servers: ['pool.ntp.org'] },
  '/api/v1/tokens': { tokens: [{ id: 1, name: 'Home Assistant', enabled: true, created_at_ms: now - 86_400_000, scopes: ['gateway:read', 'telemetry:read'] }] },
  '/api/v1/pairing': { active: false, remaining_seconds: 0, indication: 'idle' },
}

let registryGeneration = 12
let pairingTimer: ReturnType<typeof setTimeout> | undefined

// Stands in for a node joining: the real gateway closes the window as soon as
// the JOIN_REQUEST lands. Pass ?fail=1 on the open request to rehearse the
// timeout path instead.
function openPairingWindow(deviceUid: string, succeed: boolean) {
  clearTimeout(pairingTimer)
  health.pairing = { active: true, remaining_seconds: 120, indication: 'pairing' }
  pairingTimer = setTimeout(() => {
    if (succeed) {
      nodes.push({ node_id: 7 + nodes.length, device_uid: deviceUid, display_name: '', profile_id: 1, firmware: '2.1.0', state: 'active', last_seen_at_ms: Date.now(), rssi: -63, has_telemetry: true })
      registryGeneration += 1
    }
    health.pairing = { active: false, remaining_seconds: 0, indication: 'idle' }
  }, 5_000)
}

function mockApi(): Plugin {
  return {
    name: 'mock-gateway-api',
    configureServer(server) {
      server.middlewares.use((req, res, next) => {
        const path = (req.url ?? '').split('?')[0]
        if (!path.startsWith('/api') && path !== '/health') return next()
        if (path === '/api/v1/pairing/open' && req.method === 'POST') {
          let raw = ''
          req.on('data', (chunk) => { raw += chunk })
          req.on('end', () => {
            openPairingWindow(JSON.parse(raw || '{}').device_uid, !(req.url ?? '').includes('fail=1'))
            res.setHeader('Content-Type', 'application/json')
            res.end(JSON.stringify(health.pairing))
          })
          return
        }
        if (path === '/api/v1/pairing/close' && req.method === 'POST') {
          clearTimeout(pairingTimer)
          health.pairing = { active: false, remaining_seconds: 0, indication: 'idle' }
          res.setHeader('Content-Type', 'application/json')
          res.end(JSON.stringify(health.pairing))
          return
        }
        if (path === '/api/v1/nodes' && req.method === 'GET') {
          res.setHeader('Content-Type', 'application/json')
          res.end(JSON.stringify({ registry_generation: registryGeneration, nodes }))
          return
        }
        if (path === '/api/v1/radio/reset' && req.method === 'POST') {
          let raw = ''
          req.on('data', (chunk) => { raw += chunk })
          req.on('end', () => {
            const requested = JSON.parse(raw || '{}').operational_network_id
            const removed = nodes.length
            nodes.length = 0
            registryGeneration += 1
            health.radio.network_id = requested || 137
            // Stand in for the reboot so the dialog's wait actually resolves.
            setTimeout(() => { health.boot_id = 'ffeeddccbbaa9988' }, 4_000)
            res.statusCode = 202
            res.setHeader('Content-Type', 'application/json')
            res.end(JSON.stringify({ operational_network_id: health.radio.network_id, removed_nodes: removed, restarting: true }))
          })
          return
        }
        if (path === '/api/v1/nodes' && req.method === 'PATCH') {
          let raw = ''
          req.on('data', (chunk) => { raw += chunk })
          req.on('end', () => {
            const { node_id: id, display_name: name } = JSON.parse(raw || '{}')
            const node = nodes.find((entry) => entry.node_id === id)
            if (node) node.display_name = name
            registryGeneration += 1
            res.setHeader('Content-Type', 'application/json')
            res.end(JSON.stringify({ node_id: id, display_name: name, registry_generation: registryGeneration }))
          })
          return
        }
        if (path === '/api/v1/nodes' && req.method === 'DELETE') {
          let raw = ''
          req.on('data', (chunk) => { raw += chunk })
          req.on('end', () => {
            const { node_id: id } = JSON.parse(raw || '{}')
            const index = nodes.findIndex((entry) => entry.node_id === id)
            if (index >= 0) nodes.splice(index, 1)
            res.statusCode = 204
            res.end()
          })
          return
        }
        const body = routes[path] ?? {}
        res.setHeader('Content-Type', 'application/json')
        res.end(JSON.stringify(body))
      })
    },
  }
}

export default defineConfig({
  plugins: [vue(), mockApi()],
  define: { __UI_VERSION__: JSON.stringify(packageJson.version) },
})
