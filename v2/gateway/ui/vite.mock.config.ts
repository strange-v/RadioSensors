// Throwaway dev config used only to preview styling without a real gateway.
// Serves canned /api, /ui and /health responses. Not part of the build.
import vue from '@vitejs/plugin-vue'
import { defineConfig, type Plugin } from 'vite'
import packageJson from './package.json'

const now = Date.now()

const health = {
  status: 'ok', firmware: '2.1.0', api_version: 1, board: 'esp32-poe', hostname: 'osk-hub-a085e3',
  gateway_id: 'a1b2c3d4e5f60718293a4b5c6d7e8f90', boot_id: '00112233445566778899aabbccddeeff',
  reset_reason: 'power_on', uptime_seconds: 191_400, free_heap: 184_320,
  registry: { records: 6, generation: 12 },
  setup: { required: false, active: false, remaining_seconds: 0 },
  pairing: { active: false, remaining_seconds: 0, indication: 'idle' },
  storage: { ready: true, settings_generation: 4, auth_generation: 2, secrets_generation: 1 },
  ethernet: { state: 'connected', has_ip: true, ip: '192.168.1.42', mac: '50:ff:20:2e:fd:fe' },
  ota: { enabled: true, state: 'idle', progress: 0 },
  web_ui: { state: 'ok', version: '0.1.0', required_firmware: '0.8' },
  telemetry: { nodes_seen: 5, updates: 14_207, last_node_id: 3, last_received_at_ms: now - 40_000 },
  time: { state: 'synchronized', unix_ms: now, last_sync_ms: now - 1_800_000 },
  websocket: { clients: 2, connections: 9, messages_sent: 2_140, messages_dropped: 0 },
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
  // The public probe is tiny now; everything else the UI shows is behind a
  // session at /ui/status.
  '/health': { status: 'ok', boot_id: health.boot_id },
  '/ui/status': health,
  '/api/info': {
    firmware_version: '2.1.0', api_version: 1, stream_version: 1,
    gateway_id: health.gateway_id, boot_id: health.boot_id, uptime_seconds: health.uptime_seconds,
    ui: { state: 'ok', version: '0.1.0', required_firmware: '0.8' },
    board: 'esp32-poe', hostname: 'osk-hub-a085e3',
  },
  '/api/nodes': { registry_generation: 12, nodes },
  // One client that identified itself at the handshake, and one that did not,
  // so the card is exercised in both halves. The count in `health.websocket`
  // is what says how many there are; these only supply the names.
  '/ui/clients': { clients: [{ id: 3, name: 'home-assistant/0.3.0' }, { id: 4, name: '' }] },
  '/ui/setup': { setup_required: false, physical_window_active: false, remaining_seconds: 0 },
  '/ui/session': { user: { id: 1, username: 'admin', role: 'admin' }, csrf_token: 'mock-csrf' },
  '/ui/settings': { generation: 4, hostname: 'osk-hub-a085e3', mdns_enabled: true, ntp_enabled: true, pairing_window_seconds: 120, setup_window_seconds: 300, ntp_servers: ['pool.ntp.org'] },
  '/ui/pairing': { active: false, remaining_seconds: 0, indication: 'idle' },
}

let registryGeneration = 12

// Local accounts, with the same capacity and last-admin rule the gateway
// enforces, so the UI guardrails can actually be exercised here.
const users = [
  { id: 1, username: 'admin', role: 'admin', enabled: true },
  { id: 2, username: 'olena', role: 'viewer', enabled: true },
]
let nextUserId = 3

// API tokens, with the gateway's capacity and one-shot secret. These used to
// sit in the static `routes` map, which answered every method with the same
// list -- POST appeared to succeed while returning no `token` at all.
const tokens = [
  { id: 1, name: 'Home Assistant', enabled: true, created_at_ms: now - 86_400_000, scopes: ['telemetry:read'] },
]
let nextTokenId = 2
const TOKEN_SCOPES = ['telemetry:read']

function readBody(req: { on: (event: string, handler: (chunk?: unknown) => void) => void }, done: (body: Record<string, unknown>) => void) {
  let raw = ''
  req.on('data', (chunk) => { raw += chunk })
  req.on('end', () => done(JSON.parse(raw || '{}')))
}

function json(res: { statusCode: number; setHeader: (k: string, v: string) => void; end: (body?: string) => void }, status: number, body?: unknown) {
  res.statusCode = status
  if (body === undefined) { res.end(); return }
  res.setHeader('Content-Type', 'application/json')
  res.end(JSON.stringify(body))
}

const enabledAdmins = () => users.filter((user) => user.role === 'admin' && user.enabled).length
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
        if (!path.startsWith('/api') && !path.startsWith('/ui') && path !== '/health') return next()
        if (path === '/ui/pairing/open' && req.method === 'POST') {
          let raw = ''
          req.on('data', (chunk) => { raw += chunk })
          req.on('end', () => {
            openPairingWindow(JSON.parse(raw || '{}').device_uid, !(req.url ?? '').includes('fail=1'))
            res.setHeader('Content-Type', 'application/json')
            res.end(JSON.stringify(health.pairing))
          })
          return
        }
        if (path === '/ui/pairing/close' && req.method === 'POST') {
          clearTimeout(pairingTimer)
          health.pairing = { active: false, remaining_seconds: 0, indication: 'idle' }
          res.setHeader('Content-Type', 'application/json')
          res.end(JSON.stringify(health.pairing))
          return
        }
        if (path === '/ui/users') {
          if (req.method === 'GET') return json(res, 200, { users })
          if (req.method === 'POST') {
            readBody(req, (body) => {
              if (users.length >= 4) return json(res, 409, { error: 'user_capacity_reached' })
              if (users.some((user) => user.username === body.username)) return json(res, 409, { error: 'username_already_exists' })
              const created = { id: nextUserId++, username: String(body.username), role: body.role === 'admin' ? 'admin' : 'viewer', enabled: body.enabled !== false }
              users.push(created)
              json(res, 201, created)
            })
            return
          }
          if (req.method === 'PUT') {
            readBody(req, (body) => {
              const user = users.find((entry) => entry.id === body.id)
              if (!user) return json(res, 404, { error: 'user_not_found' })
              const losingLastAdmin = user.role === 'admin' && user.enabled && enabledAdmins() === 1
                && (body.role !== 'admin' || body.enabled === false)
              if (losingLastAdmin) return json(res, 409, { error: 'last_admin_required' })
              if (users.some((entry) => entry.username === body.username && entry.id !== user.id)) {
                return json(res, 409, { error: 'username_already_exists' })
              }
              Object.assign(user, { username: String(body.username), role: body.role === 'admin' ? 'admin' : 'viewer', enabled: body.enabled !== false })
              json(res, 200, user)
            })
            return
          }
          if (req.method === 'DELETE') {
            readBody(req, (body) => {
              const index = users.findIndex((entry) => entry.id === body.id)
              if (index < 0) return json(res, 404, { error: 'user_not_found' })
              const user = users[index]
              if (user.role === 'admin' && user.enabled && enabledAdmins() === 1) {
                return json(res, 409, { error: 'last_admin_required' })
              }
              users.splice(index, 1)
              json(res, 204)
            })
            return
          }
        }
        if (path === '/ui/tokens') {
          if (req.method === 'GET') return json(res, 200, { tokens })
          if (req.method === 'POST') {
            readBody(req, (body) => {
              const name = typeof body.name === 'string' ? body.name : ''
              // `scopes` is optional and the gateway grants its only scope when
              // it is absent; a value that is present is still validated.
              const scopes = body.scopes === undefined ? [...TOKEN_SCOPES] : body.scopes as string[]
              const validScopes = Array.isArray(scopes) && scopes.length > 0 &&
                scopes.every((scope) => TOKEN_SCOPES.includes(scope))
              if (Buffer.byteLength(name, 'utf8') === 0 || Buffer.byteLength(name, 'utf8') > 32 || !validScopes) {
                return json(res, 422, { error: 'invalid_token_values' })
              }
              if (tokens.length >= 8) return json(res, 409, { error: 'token_capacity_reached' })
              const created = { id: nextTokenId++, name, enabled: true, created_at_ms: Date.now(), scopes }
              tokens.push(created)
              // The secret is returned once and never stored in a readable
              // form, exactly as the gateway does it.
              const secret = Array.from({ length: 43 }, () => 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_'[Math.floor(Math.random() * 64)]).join('')
              json(res, 201, { id: created.id, name: created.name, created_at_ms: created.created_at_ms, scopes: created.scopes, token: secret })
            })
            return
          }
          if (req.method === 'DELETE') {
            readBody(req, (body) => {
              const index = tokens.findIndex((token) => token.id === body.id)
              if (index < 0) return json(res, 404, { error: 'token_not_found' })
              tokens.splice(index, 1)
              json(res, 204)
            })
            return
          }
        }
        if (path === '/api/nodes' && req.method === 'GET') {
          res.setHeader('Content-Type', 'application/json')
          res.end(JSON.stringify({ registry_generation: registryGeneration, nodes }))
          return
        }
        if (path === '/ui/radio/reset' && req.method === 'POST') {
          let raw = ''
          req.on('data', (chunk) => { raw += chunk })
          req.on('end', () => {
            const requested = JSON.parse(raw || '{}').operational_network_id
            const removed = nodes.length
            nodes.length = 0
            registryGeneration += 1
            health.radio.network_id = requested || 137
            // Stand in for the reboot so the dialog's wait actually resolves.
            setTimeout(() => { health.boot_id = 'ffeeddccbbaa99887766554433221100' }, 4_000)
            res.statusCode = 202
            res.setHeader('Content-Type', 'application/json')
            res.end(JSON.stringify({ operational_network_id: health.radio.network_id, removed_nodes: removed, restarting: true }))
          })
          return
        }
        if (path === '/ui/nodes' && req.method === 'PATCH') {
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
        if (path === '/ui/nodes' && req.method === 'DELETE') {
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
