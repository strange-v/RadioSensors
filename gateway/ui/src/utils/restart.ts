import { api } from '../api/client'

// A restarting gateway answers the request that caused it and only then goes
// down, so the probes after it fail until it is back with a new boot id.
// Without a previous id, any answer counts as back.
export async function waitForRestart(previousBootId: string, timeoutMs = 90_000) {
  const deadline = Date.now() + timeoutMs
  while (Date.now() < deadline) {
    await new Promise((resolve) => setTimeout(resolve, 2_000))
    try {
      const probe = await api.probe()
      if (probe.boot_id !== previousBootId) return
    } catch {
      // Expected while the gateway is down; keep waiting.
    }
  }
}
