// What the overview card can honestly say about the clients reading this
// gateway.
//
// It deliberately names nobody. A stream client is not attributed to the token
// it authenticated with, and a token's name is whatever the person who made it
// typed -- so the gateway cannot tell Home Assistant from a script someone
// wrote on a laptop, and the UI must not pretend otherwise. What it does know
// is that something authenticated with `telemetry:read` is streaming right
// now, and that this UI never opens a WebSocket of its own, so the count is
// not us. If the UI ever does open one, this is the place that has to start
// telling the two apart.
import type { ApiToken, Health } from '../api/types'

// 'connected'    -- something is streaming telemetry now.
// 'idle'         -- a key that could stream exists, but nothing is connected.
// 'unconfigured' -- no key can stream; no client could connect even if it tried.
// 'unknown'      -- the keys cannot be read (a viewer, or the request failed),
//                   so only the absence of a stream is certain.
export type ClientState = 'connected' | 'idle' | 'unconfigured' | 'unknown'

// The WebSocket handshake requires `telemetry:read` (API.md, "WebSocket
// bootstrap"), so that scope -- not the key's name -- is what decides whether
// a client could stream at all.
export function canStreamTelemetry(token: ApiToken): boolean {
  return token.enabled && token.scopes.includes('telemetry:read')
}

// `tokens` is null when this session may not read them.
export function clientState(health: Health | null, tokens: ApiToken[] | null): ClientState {
  if ((health?.websocket.clients ?? 0) > 0) return 'connected'
  if (tokens === null) return 'unknown'
  return tokens.some(canStreamTelemetry) ? 'idle' : 'unconfigured'
}

// Only the state that says no client could ever connect is worth a call to
// action, and only an admin can act on it -- keys are made on the
// administration page, which a viewer cannot reach.
export function offersSetup(state: ClientState): boolean {
  return state === 'unconfigured'
}
