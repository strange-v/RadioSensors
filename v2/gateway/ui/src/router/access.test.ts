import { describe, expect, it, vi } from 'vitest'
import { resolveRouteAccess, type RouteAccessApi } from './access'

function gatewayApi(setupRequired: boolean, sessionAvailable = false): RouteAccessApi {
  return {
    setupStatus: vi.fn().mockResolvedValue({
      setup_required: setupRequired,
      physical_window_active: false,
      remaining_seconds: 0,
    }),
    session: sessionAvailable
      ? vi.fn().mockResolvedValue({})
      : vi.fn().mockRejectedValue(new Error('authentication_required')),
  } as RouteAccessApi
}

describe('gateway route access', () => {
  it('redirects a stale login URL to setup after a factory reset', async () => {
    await expect(resolveRouteAccess(
      { path: '/login', fullPath: '/login', requiresAuth: false },
      gatewayApi(true),
    )).resolves.toEqual({ path: '/setup' })
  })

  it('keeps login available after setup is complete', async () => {
    await expect(resolveRouteAccess(
      { path: '/login', fullPath: '/login', requiresAuth: false },
      gatewayApi(false),
    )).resolves.toBe(true)
  })

  it('redirects an unauthenticated protected URL to setup when required', async () => {
    await expect(resolveRouteAccess(
      { path: '/nodes', fullPath: '/nodes', requiresAuth: true },
      gatewayApi(true),
    )).resolves.toEqual({ path: '/setup' })
  })

  it('preserves the requested URL when normal login is required', async () => {
    await expect(resolveRouteAccess(
      { path: '/nodes', fullPath: '/nodes?selected=2', requiresAuth: true },
      gatewayApi(false),
    )).resolves.toEqual({ path: '/login', query: { redirect: '/nodes?selected=2' } })
  })
})
