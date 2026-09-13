import { readFileSync } from 'node:fs'
import { describe, expect, it } from 'vitest'
import { COUNT_MAX, RADIO_POWER_MAX, commandArguments, commandValue, parseCommandValue, supportedCommands } from './commands'

interface ManifestField { name: string; valid_raw?: { min: number; max: number } }
interface Manifest {
  commands: { types: Array<{ type: number; name: string; arguments: ManifestField[] }> }
  telemetry: { profiles: Array<{ id: number; commands: number[] }> }
}

const manifest = JSON.parse(readFileSync(
  new URL('../../../../protocol/protocol-manifest.json', import.meta.url), 'utf8')) as Manifest

describe('command catalogue', () => {
  it('offers exactly the commands the protocol manifest lists per profile', () => {
    const names = new Map(manifest.commands.types.map((type) => [type.type, type.name]))
    for (const profile of manifest.telemetry.profiles) {
      expect(supportedCommands(profile.id), `profile ${profile.id}`)
        .toEqual(profile.commands.map((type) => names.get(type)))
    }
    expect(supportedCommands(0)).toEqual([])
    expect(supportedCommands(99)).toEqual([])
  })

  it('bounds the radio power level like the manifest', () => {
    const power = manifest.commands.types.find((type) => type.name === 'set_radio_power')
    expect(power?.arguments[0].valid_raw?.max).toBe(RADIO_POWER_MAX)
  })
})

describe('parseCommandValue', () => {
  it('accepts whole numbers within the type range', () => {
    expect(parseCommandValue('set_radio_power', '0')).toBe(0)
    expect(parseCommandValue('set_radio_power', ' 31 ')).toBe(31)
    expect(parseCommandValue('set_count', '4294967295')).toBe(COUNT_MAX)
  })

  it('rejects anything else', () => {
    for (const text of ['', '32', '-1', '1e3', '12.5', '0x10', 'abc']) {
      expect(parseCommandValue('set_radio_power', text), text).toBeNull()
    }
    expect(parseCommandValue('set_count', '4294967296')).toBeNull()
  })
})

describe('command arguments', () => {
  it('names the argument after the command type', () => {
    expect(commandArguments('set_radio_power', 16)).toEqual({ power_level: 16 })
    expect(commandArguments('set_count', 1234)).toEqual({ count: 1234 })
    expect(commandValue({ arguments: { count: 0 } })).toBe(0)
  })
})
