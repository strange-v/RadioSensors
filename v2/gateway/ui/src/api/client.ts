import { computed, ref } from 'vue'
import type { ApiTokenList, CreatedApiToken, GatewayInfo, GatewaySettings, Health, NodeRegistry, PairingStatus, RadioNetworkReset, RenamedNode, Session, SessionUser, SetupRequest, SetupStatus, TokenScope } from './types'

export class ApiError extends Error { constructor(public status: number, public code: string) { super(code) } }

// Whether the gateway is answering at all. Every request updates it, so the
// shell can report a dropped connection without polling for it: an HTTP error
// still means the gateway replied, only a transport failure means it did not.
export const gatewayReachable = ref(true)

// Flash writes and password hashing can make a gateway mutation noticeably
// slower than an ordinary read. This is the floor for interactive calls; pass
// a larger value for anything slower still.
const REQUEST_TIMEOUT_MS = 30_000

// Reads on a timer get a tighter budget instead: waiting half a minute for a
// value that will be asked for again in ten seconds only stacks work on a
// gateway that is already slow.
const POLL_TIMEOUT_MS = 8_000

async function request<T>(path: string, init?: RequestInit, timeoutMs = REQUEST_TIMEOUT_MS): Promise<T> {
  const controller = new AbortController()
  const timeout = globalThis.setTimeout(() => controller.abort(), timeoutMs)
  try {
    const response = await fetch(path, { ...init, credentials: 'same-origin', signal: controller.signal, headers: { Accept: 'application/json', ...(init?.body ? { 'Content-Type': 'application/json' } : {}), ...(csrfToken && init?.method && !['GET', 'HEAD'].includes(init.method) ? { 'X-CSRF-Token': csrfToken } : {}), ...init?.headers } })
    gatewayReachable.value = true
    const body = await response.json().catch(() => ({})) as { error?: string }
    if (!response.ok) throw new ApiError(response.status, body.error ?? 'generic')
    return body as T
  } catch (error) {
    if (!(error instanceof ApiError)) gatewayReachable.value = false
    throw error
  } finally { globalThis.clearTimeout(timeout) }
}

let csrfToken = ''

// Who is signed in, so the UI can hide admin-only actions rather than offer
// buttons that always come back 403.
export const sessionUser = ref<SessionUser | null>(null)
export const isAdmin = computed(() => sessionUser.value?.role === 'admin')

function rememberSession(session: Session): Session {
  csrfToken = session.csrf_token
  sessionUser.value = session.user
  return session
}

export const api = {
  setupStatus: () => request<SetupStatus>('/api/v1/setup'),
  setup: async (payload: SetupRequest) => rememberSession(await request<Session>('/api/v1/setup', { method: 'POST', body: JSON.stringify(payload) })),
  login: async (username: string, password: string) => rememberSession(await request<Session>('/api/v1/session', { method: 'POST', body: JSON.stringify({ username, password }) })),
  session: async () => rememberSession(await request<Session>('/api/v1/session')),
  logout: async () => { await request<void>('/api/v1/session', { method: 'DELETE' }); csrfToken = ''; sessionUser.value = null },
  info: () => request<GatewayInfo>('/api/v1/info'),
  health: () => request<Health>('/health'),
  nodes: () => request<NodeRegistry>('/api/v1/nodes'),
  // Same reads, used from the refresh timers.
  poll: {
    health: () => request<Health>('/health', undefined, POLL_TIMEOUT_MS),
    info: () => request<GatewayInfo>('/api/v1/info', undefined, POLL_TIMEOUT_MS),
    nodes: () => request<NodeRegistry>('/api/v1/nodes', undefined, POLL_TIMEOUT_MS),
  },
  renameNode: (nodeId: number, displayName: string) => request<RenamedNode>('/api/v1/nodes', { method: 'PATCH', body: JSON.stringify({ node_id: nodeId, display_name: displayName }) }),
  deleteNode: (nodeId: number) => request<void>('/api/v1/nodes', { method: 'DELETE', body: JSON.stringify({ node_id: nodeId }) }),
  settings: () => request<GatewaySettings>('/api/v1/settings'),
  updateSettings: (settings: Omit<GatewaySettings, 'generation'>) => request<GatewaySettings>('/api/v1/settings', { method: 'PUT', body: JSON.stringify(settings) }),
  openPairing: (deviceUid: string, factoryKey: string) => request<PairingStatus>('/api/v1/pairing/open', { method: 'POST', body: JSON.stringify({ device_uid: deviceUid, factory_key: factoryKey }) }),
  closePairing: () => request<PairingStatus>('/api/v1/pairing/close', { method: 'POST' }),
  // Omitting the id lets the gateway generate one. The gateway restarts right
  // after answering, so this call is the last one the session can make.
  resetRadioNetwork: (networkId?: number) => request<RadioNetworkReset>('/api/v1/radio/reset', { method: 'POST', body: JSON.stringify(networkId ? { operational_network_id: networkId } : {}) }),
  tokens: () => request<ApiTokenList>('/api/v1/tokens'),
  createToken: (name: string, scopes: TokenScope[]) => request<CreatedApiToken>('/api/v1/tokens', { method: 'POST', body: JSON.stringify({ name, scopes }) }),
  deleteToken: (id: number) => request<void>('/api/v1/tokens', { method: 'DELETE', body: JSON.stringify({ id }) }),
}

export function errorCode(error: unknown): string {
  if (error instanceof ApiError) return error.code
  if (error instanceof DOMException && error.name === 'AbortError') return 'timeout'
  if (error instanceof TypeError) return 'network'
  return 'generic'
}
