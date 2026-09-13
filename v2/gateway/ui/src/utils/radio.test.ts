import { describe, expect, it } from 'vitest'
import { parsePowerLevel } from './radio'

describe('parsePowerLevel', () => {
  it('accepts whole numbers up to the ceiling', () => {
    expect(parsePowerLevel('0', 2)).toBe(0)
    expect(parsePowerLevel(' 2 ', 2)).toBe(2)
    expect(parsePowerLevel('31', 31)).toBe(31)
  })

  it('rejects anything else', () => {
    for (const text of ['', '3', '-1', '1.5', '1e1', 'x', '100']) {
      expect(parsePowerLevel(text, 2), text).toBeNull()
    }
  })
})
