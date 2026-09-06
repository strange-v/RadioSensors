import { ref } from 'vue'
import type { ApiTokenList, CreatedApiToken, GatewayInfo, GatewaySettings, Health, NodeRegistry, PairingStatus, Session, SetupRequest, SetupStatus, TokenScope } from './types'

export class ApiError extends Error { constructor(public status: number, public code: string) { super(code) } }

// Whether the gateway is answering at all. Every request updates it, so the
// shell can report a dropped connection without polling for it: an HTTP error
// still means the gateway replied, only a transport failure means it did not.
export const gatewayReachable = ref(true)

async function request<T>(path: string, init?: RequestInit): Promise<T> {
  const controller = new AbortController()
  const timeout = globalThis.setTimeout(() => controller.abort(), 8000)
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

function rememberSession(session: Session): Session {
  csrfToken = session.csrf_token
  return session
}

export const api = {
  setupStatus: () => request<SetupStatus>('/api/v1/setup'),
  setup: async (payload: SetupRequest) => rememberSession(await request<Session>('/api/v1/setup', { method: 'POST', body: JSON.stringify(payload) })),
  login: async (username: string, password: string) => rememberSession(await request<Session>('/api/v1/session', { method: 'POST', body: JSON.stringify({ username, password }) })),
  session: async () => rememberSession(await request<Session>('/api/v1/session')),
  logout: async () => { await request<void>('/api/v1/session', { method: 'DELETE' }); csrfToken = '' },
  info: () => request<GatewayInfo>('/api/v1/info'),
  health: () => request<Health>('/health'),
  nodes: () => request<NodeRegistry>('/api/v1/nodes'),
  settings: () => request<GatewaySettings>('/api/v1/settings'),
  updateSettings: (settings: Omit<GatewaySettings, 'generation'>) => request<GatewaySettings>('/api/v1/settings', { method: 'PUT', body: JSON.stringify(settings) }),
  openPairing: (deviceUid: string, factoryKey: string) => request<PairingStatus>('/api/v1/pairing/open', { method: 'POST', body: JSON.stringify({ device_uid: deviceUid, factory_key: factoryKey }) }),
  closePairing: () => request<PairingStatus>('/api/v1/pairing/close', { method: 'POST' }),
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
