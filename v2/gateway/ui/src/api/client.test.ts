import { afterEach, describe, expect, it, vi } from 'vitest'
import { ApiError, api, errorCode } from './client'
afterEach(() => vi.unstubAllGlobals())
describe('gateway API client', () => {
  it('reads setup status', async () => { vi.stubGlobal('fetch', vi.fn().mockResolvedValue(new Response(JSON.stringify({ setup_required: true, physical_window_active: false, remaining_seconds: 0 }), { status: 200, headers: { 'Content-Type': 'application/json' } }))); await expect(api.setupStatus()).resolves.toMatchObject({ setup_required: true }); expect(fetch).toHaveBeenCalledWith('/api/v1/setup', expect.objectContaining({ signal: expect.any(AbortSignal) })) })
  it('preserves firmware error codes', async () => { vi.stubGlobal('fetch', vi.fn().mockResolvedValue(new Response(JSON.stringify({ error: 'physical_setup_required' }), { status: 403 }))); await expect(api.setup({ username: 'admin', password: 'password' })).rejects.toEqual(new ApiError(403, 'physical_setup_required')) })
  it('maps network errors', () => expect(errorCode(new TypeError('fetch failed'))).toBe('network'))
  it('uses the session CSRF token for logout', async () => {
    const fetchMock = vi.fn()
      .mockResolvedValueOnce(new Response(JSON.stringify({ user: { id: 1, username: 'admin', role: 'admin' }, csrf_token: 'csrf-value' }), { status: 200, headers: { 'Content-Type': 'application/json' } }))
      .mockResolvedValueOnce(new Response(null, { status: 204 }))
    vi.stubGlobal('fetch', fetchMock)
    await api.login('admin', 'password')
    await api.logout()
    expect(fetchMock).toHaveBeenLastCalledWith('/api/v1/session', expect.objectContaining({ method: 'DELETE', credentials: 'same-origin', headers: expect.objectContaining({ 'X-CSRF-Token': 'csrf-value' }) }))
  })
  it('uses CSRF and read-only scopes when creating a Home Assistant token', async () => {
    const fetchMock = vi.fn()
      .mockResolvedValueOnce(new Response(JSON.stringify({ user: { id: 1, username: 'admin', role: 'admin' }, csrf_token: 'csrf-value' }), { status: 200, headers: { 'Content-Type': 'application/json' } }))
      .mockResolvedValueOnce(new Response(JSON.stringify({ id: 1, name: 'Home Assistant', enabled: true, created_at_ms: 1, scopes: ['gateway:read', 'registry:read', 'telemetry:read'], token: 'secret' }), { status: 201, headers: { 'Content-Type': 'application/json' } }))
    vi.stubGlobal('fetch', fetchMock)
    await api.login('admin', 'password')
    await api.createToken('Home Assistant', ['gateway:read', 'registry:read', 'telemetry:read'])
    expect(fetchMock).toHaveBeenLastCalledWith('/api/v1/tokens', expect.objectContaining({ method: 'POST', headers: expect.objectContaining({ 'X-CSRF-Token': 'csrf-value' }), body: JSON.stringify({ name: 'Home Assistant', scopes: ['gateway:read', 'registry:read', 'telemetry:read'] }) }))
  })
})
