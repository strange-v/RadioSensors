// What the overview card can honestly say about the clients reading this
// gateway.
//
// A key's name proves nothing -- it is whatever the person who made it typed,
// and the gateway accepts duplicates -- so no state here is ever derived from
// one. Naming a connected client is the job of the identity it sends at the
// handshake, which arrives separately (see WEBSOCKET.md). These states answer
// the smaller question the keys alone can settle: is there anything set up at
// all, and is anything reading right now.
import type { ApiToken, Health } from '../api/types'

// 'connected'    -- something is streaming telemetry now.
// 'idle'         -- a key that could stream exists, but nothing is connected.
// 'unconfigured' -- no key can stream; nobody has set a client up at all.
// 'unknown'      -- the keys cannot be read (a viewer, or the request failed),
//                   so only the absence of a stream is certain.
export type ClientState = 'connected' | 'idle' | 'unconfigured' | 'unknown'

// The WebSocket handshake requires `telemetry:read` (API.md, "WebSocket
// bootstrap"), so that scope -- not the key's name -- is what decides whether
// a client could stream at all.
export function canStreamTelemetry(token: ApiToken): boolean {
  return token.enabled && token.scopes.includes('telemetry:read')
}

// Keys are checked before the stream, because "no key exists" is the one thing
// here that is certain rather than inferred, and it survives a reboot: keys
// live in flash, so this does not briefly claim an unconfigured gateway while
// a client is still reconnecting.
//
// The stream can carry a browser session as well as a key (see
// authorizeTelemetryStream in WebServer.cpp), so a gateway with no keys and
// a live socket is someone's browser, not a configured client. Reporting
// 'unconfigured' there is right: there is still nothing set up.
//
// `tokens` is null when this session may not read them. A viewer still gets
// 'connected' when something is streaming -- that much is readable without the
// keys -- but never the setup prompt, which it could not act on.
export function clientState(health: Health | null, tokens: ApiToken[] | null): ClientState {
  const streaming = (health?.websocket.clients ?? 0) > 0
  if (tokens === null) return streaming ? 'connected' : 'unknown'
  if (!tokens.some(canStreamTelemetry)) return 'unconfigured'
  return streaming ? 'connected' : 'idle'
}

// Only the state that says no client could ever connect is worth a call to
// action, and only an admin can act on it -- keys are made on the
// administration page, which a viewer cannot reach.
export function offersSetup(state: ClientState): boolean {
  return state === 'unconfigured'
}
