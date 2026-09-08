import { computed, ref } from 'vue'
import type { ApiTokenList, CreatedApiToken, GatewayProbe, StreamClientList, GatewayInfo, GatewaySettings, GatewayUser, Health, NodeRegistry, PairingStatus, RadioNetworkReset, RenamedNode, Session, SessionUser, SetupRequest, SetupStatus, UserList, UserWrite } from './types'

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

// Drops the local half of a session the gateway has already revoked -- a user
// mutation on your own account does exactly that, so DELETE /session would
// only answer 401.
export function forgetSession() {
  csrfToken = ''
  sessionUser.value = null
}

export const api = {
  setupStatus: () => request<SetupStatus>('/ui/setup'),
  setup: async (payload: SetupRequest) => rememberSession(await request<Session>('/ui/setup', { method: 'POST', body: JSON.stringify(payload) })),
  login: async (username: string, password: string) => rememberSession(await request<Session>('/ui/session', { method: 'POST', body: JSON.stringify({ username, password }) })),
  session: async () => rememberSession(await request<Session>('/ui/session')),
  logout: async () => { await request<void>('/ui/session', { method: 'DELETE' }); csrfToken = ''; sessionUser.value = null },
  info: () => request<GatewayInfo>('/api/info'),
  nodes: () => request<NodeRegistry>('/api/nodes'),
  status: () => request<Health>('/ui/status'),
  // The public liveness probe carries only `status` and `boot_id`, and it is
  // the one read that still works when no session does. That is the whole
  // reason it exists: a radio reset reboots the gateway and takes every
  // session with it. Everything else uses status().
  probe: () => request<GatewayProbe>('/health', undefined, POLL_TIMEOUT_MS),
  // Same reads, used from the refresh timers.
  poll: {
    status: () => request<Health>('/ui/status', undefined, POLL_TIMEOUT_MS),
    info: () => request<GatewayInfo>('/api/info', undefined, POLL_TIMEOUT_MS),
    nodes: () => request<NodeRegistry>('/api/nodes', undefined, POLL_TIMEOUT_MS),
    clients: () => request<StreamClientList>('/ui/clients', undefined, POLL_TIMEOUT_MS),
  },
  renameNode: (nodeId: number, displayName: string) => request<RenamedNode>('/ui/nodes', { method: 'PATCH', body: JSON.stringify({ node_id: nodeId, display_name: displayName }) }),
  deleteNode: (nodeId: number) => request<void>('/ui/nodes', { method: 'DELETE', body: JSON.stringify({ node_id: nodeId }) }),
  settings: () => request<GatewaySettings>('/ui/settings'),
  updateSettings: (settings: Omit<GatewaySettings, 'generation'>) => request<GatewaySettings>('/ui/settings', { method: 'PUT', body: JSON.stringify(settings) }),
  openPairing: (deviceUid: string, factoryKey: string) => request<PairingStatus>('/ui/pairing/open', { method: 'POST', body: JSON.stringify({ device_uid: deviceUid, factory_key: factoryKey }) }),
  closePairing: () => request<PairingStatus>('/ui/pairing/close', { method: 'POST' }),
  // Omitting the id lets the gateway generate one. The gateway restarts right
  // after answering, so this call is the last one the session can make.
  resetRadioNetwork: (networkId?: number) => request<RadioNetworkReset>('/ui/radio/reset', { method: 'POST', body: JSON.stringify(networkId ? { operational_network_id: networkId } : {}) }),
  users: () => request<UserList>('/ui/users'),
  createUser: (user: UserWrite & { password: string }) => request<GatewayUser>('/ui/users', { method: 'POST', body: JSON.stringify(user) }),
  // Replaces username, role and enabled; a password is only sent when set.
  // Every session of that user is revoked, including this one when it is you.
  updateUser: (id: number, user: UserWrite) => request<GatewayUser>('/ui/users', { method: 'PUT', body: JSON.stringify({ id, ...user }) }),
  deleteUser: (id: number) => request<void>('/ui/users', { method: 'DELETE', body: JSON.stringify({ id }) }),
  tokens: () => request<ApiTokenList>('/ui/tokens'),
  // `scopes` is omitted deliberately: the gateway grants its only scope when
  // the field is absent, and sending it would make this the second place that
  // has to be edited when the set of scopes changes.
  createToken: (name: string) => request<CreatedApiToken>('/ui/tokens', { method: 'POST', body: JSON.stringify({ name }) }),
  deleteToken: (id: number) => request<void>('/ui/tokens', { method: 'DELETE', body: JSON.stringify({ id }) }),
}

export function errorCode(error: unknown): string {
  if (error instanceof ApiError) return error.code
  if (error instanceof DOMException && error.name === 'AbortError') return 'timeout'
  if (error instanceof TypeError) return 'network'
  return 'generic'
}
