// Throwaway dev config used only to preview styling without a real gateway.
// Serves canned /api and /health responses. Not part of the build.
import vue from '@vitejs/plugin-vue'
import { defineConfig, type Plugin } from 'vite'
import packageJson from './package.json'

const now = Date.now()

const health = {
  status: 'ok', firmware: '2.1.0', api_version: 1, board: 'esp32-poe', hostname: 'rf-gateway-a085e3',
  reset_reason: 'power_on', uptime_ms: 191_400_000, free_heap: 184_320,
  registry: { records: 6, generation: 12 },
  setup: { required: false, active: false, remaining_seconds: 0 },
  pairing: { active: false, remaining_seconds: 0, indication: 'idle' },
  storage: { ready: true, settings_generation: 4, auth_generation: 2, secrets_generation: 1 },
  ethernet: { state: 'connected', has_ip: true, ip: '192.168.1.42', mac: '50:ff:20:2e:fd:fe' },
  ota: { enabled: true, state: 'idle', progress: 0 },
  web_ui: { state: 'ok', version: '0.1.0', required_api_version: 1 },
  telemetry: { nodes_seen: 5, updates: 14_207, last_node_id: 3, last_received_at_ms: now - 40_000 },
  time: { state: 'synced', unix_ms: now, last_sync_ms: now - 1_800_000 },
  websocket: { clients: 1, connections: 9, messages_sent: 2_140, messages_dropped: 0 },
  radio: {
    state: 'ready', present: true, version: 36, frequency_band_mhz: '868', frequency_hz: 868_000_000,
    bit_rate: 55_555, node_id: 1, network_id: 172, variant: 'RFM69HCW', configured_power_dbm: 14,
    encryption_enabled: true, profile: 'balanced', spi_host: 'VSPI', pins: {}, counters: {},
    last_packet: { at_ms: now - 40_000, sender_id: 3, rssi: -74 },
  },
}

const nodes = [
  { node_id: 2, profile_id: 1, firmware: '2.1.0', state: 'active', name: 'Кухня', last_seen_at_ms: now - 120_000, rssi: -68, has_telemetry: true },
  { node_id: 3, profile_id: 1, firmware: '2.1.0', state: 'active', name: 'Гараж', last_seen_at_ms: now - 40_000, rssi: -74, has_telemetry: true },
  { node_id: 4, profile_id: 2, firmware: '2.0.4', state: 'pending', name: 'Підвал', last_seen_at_ms: now - 5_400_000, rssi: -91, has_telemetry: false },
  { node_id: 5, profile_id: 1, firmware: '2.1.0', state: 'active', name: 'Тепличка', last_seen_at_ms: now - 300_000, rssi: -59, has_telemetry: true },
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

function mockApi(): Plugin {
  return {
    name: 'mock-gateway-api',
    configureServer(server) {
      server.middlewares.use((req, res, next) => {
        const path = (req.url ?? '').split('?')[0]
        if (!path.startsWith('/api') && path !== '/health') return next()
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
