// Relative "last seen" formatting shared by the overview and the node list so
// both read the same way. Takes the translator rather than importing i18n, to
// stay usable from any component and testable without a live app instance.
type Translate = (key: string, named?: Record<string, unknown>) => string

export function lastSeen(t: Translate, atMs: number | undefined): string {
  if (!atMs) return '—'
  const minutes = Math.max(0, Math.floor((Date.now() - atMs) / 60_000))
  if (minutes < 1) return t('time.underMinute')
  if (minutes < 60) return t('time.minutes', { value: minutes })
  const hours = Math.floor(minutes / 60)
  if (hours < 24) return t('time.hours', { value: hours })
  return t('time.days', { value: Math.floor(hours / 24) })
}

export function signal(rssi: number | undefined): string {
  return rssi === undefined ? '—' : `${rssi} dBm`
}
