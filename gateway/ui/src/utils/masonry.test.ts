import { describe, expect, it } from 'vitest'
import { MASONRY_ROW_PX, masonrySpan } from './masonry'

describe('masonrySpan', () => {
  it('covers the card height plus the gap below it', () => {
    expect(masonrySpan(100, 16)).toBe(29)
    expect(masonrySpan(100, 16) * MASONRY_ROW_PX).toBeGreaterThanOrEqual(116)
  })

  it('rounds partial rows up', () => {
    expect(masonrySpan(101, 16)).toBe(30)
  })

  it('spans at least one row', () => {
    expect(masonrySpan(0, 0)).toBe(1)
  })
})
