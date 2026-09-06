// Cleanup and parsing helpers for manually entered pairing credentials
// (device UID and factory key). Both are fixed-length hex strings; see
// POST /api/v1/pairing/open in API.md.
export const PAIRING_UID_HEX_LENGTH = 20 // 10-byte device UID
export const PAIRING_KEY_HEX_LENGTH = 32 // 16-byte factory key

// Cyrillic letters that are visually identical to Latin hex digits on a
// standard keyboard layout switch (А/а, В/в, С/с, Е/е only — the remaining
// hex digits have no common Cyrillic look-alike).
const CYRILLIC_TO_LATIN: Record<string, string> = { А: 'A', а: 'A', В: 'B', в: 'B', С: 'C', с: 'C', Е: 'E', е: 'E' }

// Strips everything but hex digits, mapping look-alike Cyrillic letters and
// separators/quotes/whitespace away first.
export function normalizeHexFragment(input: string): string {
  let result = ''
  for (const char of input) {
    const upper = (CYRILLIC_TO_LATIN[char] ?? char).toUpperCase()
    if (upper >= '0' && upper <= '9') result += upper
    else if (upper >= 'A' && upper <= 'F') result += upper
  }
  return result
}

export interface ParsedPairingCredentials {
  uid?: string
  key?: string
}

function readField(source: unknown, keys: string[]): string | undefined {
  if (typeof source !== 'object' || source === null) return undefined
  const record = source as Record<string, unknown>
  for (const key of keys) {
    const value = record[key]
    if (typeof value === 'string') return value
  }
  return undefined
}

// Makes sense of a single pasted blob: full JSON from the pairing QR/label
// export, a loose "device_uid: ..., factory_key: ..." text, a single
// credential, or both concatenated back-to-back.
export function parsePairingPaste(text: string): ParsedPairingCredentials {
  const trimmed = text.trim()
  if (!trimmed) return {}

  try {
    const parsed: unknown = JSON.parse(trimmed)
    const uidRaw = readField(parsed, ['device_uid', 'deviceUid', 'uid'])
    const keyRaw = readField(parsed, ['factory_key', 'factoryKey', 'key'])
    if (uidRaw !== undefined || keyRaw !== undefined) {
      return {
        uid: uidRaw ? normalizeHexFragment(uidRaw).slice(0, PAIRING_UID_HEX_LENGTH) : undefined,
        key: keyRaw ? normalizeHexFragment(keyRaw).slice(0, PAIRING_KEY_HEX_LENGTH) : undefined,
      }
    }
  } catch {
    // Not strict JSON — fall through to text heuristics below.
  }

  const uidMatch = trimmed.match(/device[_-]?uid["'\s:=]+([0-9a-fA-FА-Яа-я\-:\s]{10,})/i)
  const keyMatch = trimmed.match(/factory[_-]?key["'\s:=]+([0-9a-fA-FА-Яа-я\-:\s]{10,})/i)
  if (uidMatch || keyMatch) {
    return {
      uid: uidMatch ? normalizeHexFragment(uidMatch[1]).slice(0, PAIRING_UID_HEX_LENGTH) : undefined,
      key: keyMatch ? normalizeHexFragment(keyMatch[1]).slice(0, PAIRING_KEY_HEX_LENGTH) : undefined,
    }
  }

  const clean = normalizeHexFragment(trimmed)
  if (clean.length === PAIRING_UID_HEX_LENGTH) return { uid: clean }
  if (clean.length === PAIRING_KEY_HEX_LENGTH) return { key: clean }
  if (clean.length === PAIRING_UID_HEX_LENGTH + PAIRING_KEY_HEX_LENGTH) {
    return { uid: clean.slice(0, PAIRING_UID_HEX_LENGTH), key: clean.slice(PAIRING_UID_HEX_LENGTH) }
  }
  return {}
}
