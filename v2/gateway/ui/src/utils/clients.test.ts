import { describe, expect, it } from 'vitest'
import type { ApiToken, Health } from '../api/types'
import { canStreamTelemetry, clientState, offersSetup } from './clients'

const health = (clients: number) => ({ websocket: { clients, connections: 1, messages_sent: 10, messages_dropped: 0 } } as Health)
const token = (over: Partial<ApiToken> = {}): ApiToken =>
  ({ id: 1, name: 'Home Assistant', enabled: true, created_at_ms: 1_700_000_000_000, scopes: ['gateway:read', 'registry:read', 'telemetry:read'], ...over })

describe('canStreamTelemetry', () => {
  it('requires the scope the WebSocket handshake requires', () => {
    expect(canStreamTelemetry(token())).toBe(true)
    expect(canStreamTelemetry(token({ scopes: ['telemetry:read'] }))).toBe(true)
    expect(canStreamTelemetry(token({ scopes: ['gateway:read', 'registry:read'] }))).toBe(false)
  })

  it('ignores a disabled key', () => {
    expect(canStreamTelemetry(token({ enabled: false }))).toBe(false)
  })
})

describe('clientState', () => {
  it('reports a live stream regardless of what the keys say', () => {
    expect(clientState(health(1), [])).toBe('connected')
    expect(clientState(health(2), null)).toBe('connected')
  })

  it('separates a key that is not being used from no key at all', () => {
    expect(clientState(health(0), [token()])).toBe('idle')
    expect(clientState(health(0), [])).toBe('unconfigured')
    expect(clientState(health(0), [token({ scopes: ['gateway:read'] })])).toBe('unconfigured')
  })

  it('counts a key under any name, because a name proves nothing about the client', () => {
    expect(clientState(health(0), [token({ name: 'Хата' })])).toBe('idle')
  })

  it('says only what it knows when the keys are unreadable', () => {
    // A viewer, or a failed request: the stream is still readable, so the
    // absence of a connection is certain and nothing else is.
    expect(clientState(health(0), null)).toBe('unknown')
    expect(clientState(null, null)).toBe('unknown')
  })
})

describe('offersSetup', () => {
  it('invites setup only where there is setting up to do', () => {
    expect(offersSetup('unconfigured')).toBe(true)
    for (const state of ['connected', 'idle', 'unknown'] as const) {
      expect(offersSetup(state)).toBe(false)
    }
  })
})
