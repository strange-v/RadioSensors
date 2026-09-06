import { describe, expect, it } from 'vitest'
import { normalizeHexFragment, parsePairingPaste } from './hexCredentials'

describe('normalizeHexFragment', () => {
  it('strips separators, quotes, and whitespace', () => {
    expect(normalizeHexFragment('10-21-32-43-54-65-76-87-98-A9')).toBe('102132435465768798A9')
  })
  it('uppercases lowercase hex', () => {
    expect(normalizeHexFragment('a9f0')).toBe('A9F0')
  })
  it('maps look-alike Cyrillic letters to Latin hex digits', () => {
    expect(normalizeHexFragment('АВСЕавсе')).toBe('ABCEABCE')
  })
  it('drops characters outside 0-9A-F', () => {
    expect(normalizeHexFragment('10G21Z32')).toBe('102132')
  })
})

describe('parsePairingPaste', () => {
  it('reads device_uid and factory_key from JSON', () => {
    const json = JSON.stringify({ device_uid: '102132435465768798A9', factory_key: '00112233445566778899AABBCCDDEEFF' })
    expect(parsePairingPaste(json)).toEqual({ uid: '102132435465768798A9', key: '00112233445566778899AABBCCDDEEFF' })
  })
  it('reads camelCase JSON keys', () => {
    const json = JSON.stringify({ deviceUid: '102132435465768798a9', factoryKey: '00112233445566778899aabbccddeeff' })
    expect(parsePairingPaste(json)).toEqual({ uid: '102132435465768798A9', key: '00112233445566778899AABBCCDDEEFF' })
  })
  it('reads a loose "key: value" text block', () => {
    const text = 'device_uid: 10-21-32-43-54-65-76-87-98-A9\nfactory_key: 00112233445566778899AABBCCDDEEFF'
    expect(parsePairingPaste(text)).toEqual({ uid: '102132435465768798A9', key: '00112233445566778899AABBCCDDEEFF' })
  })
  it('assigns a bare 20-character hex blob to the UID', () => {
    expect(parsePairingPaste('10-21-32-43-54-65-76-87-98-A9')).toEqual({ uid: '102132435465768798A9' })
  })
  it('assigns a bare 32-character hex blob to the key', () => {
    expect(parsePairingPaste('00112233445566778899AABBCCDDEEFF')).toEqual({ key: '00112233445566778899AABBCCDDEEFF' })
  })
  it('splits a concatenated uid+key blob', () => {
    const combined = '102132435465768798A900112233445566778899AABBCCDDEEFF'
    expect(parsePairingPaste(combined)).toEqual({
      uid: '102132435465768798A9',
      key: '00112233445566778899AABBCCDDEEFF',
    })
  })
  it('returns nothing for unrelated text', () => {
    expect(parsePairingPaste('hello world')).toEqual({})
  })
})
