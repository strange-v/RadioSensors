// A fixed transmit level: a whole number within the node's ceiling. The
// gateway refuses anything else with `invalid_power_policy`.
export function parsePowerLevel(text: string, max: number): number | null {
  const trimmed = text.trim()
  if (!/^\d{1,2}$/.test(trimmed)) return null
  const value = Number(trimmed)
  return value <= max ? value : null
}
