import type { CommandArguments, CommandType, NodeCommand } from '../api/types'

// Mirrors the per-profile `commands` lists in v2/protocol/protocol-manifest.json;
// commands.test.ts holds the two together. The gateway refuses anything else
// with `unsupported_command`, so this only decides what the form offers.
const PROFILE_COMMANDS: Record<number, CommandType[]> = {
  1: ['set_radio_power'],
  2: ['set_radio_power'],
  3: ['set_radio_power'],
  4: ['set_radio_power'],
  5: ['set_radio_power'],
  6: ['set_radio_power', 'set_count'],
  7: ['set_radio_power'],
  8: ['set_radio_power'],
}

export const RADIO_POWER_MAX = 31
export const COUNT_MAX = 0xffff_ffff

export function supportedCommands(profileId: number): CommandType[] {
  return PROFILE_COMMANDS[profileId] ?? []
}

// Whole decimal numbers only: "1e3" or "12.5" would reach the node as
// something other than what was typed.
export function parseCommandValue(type: CommandType, text: string): number | null {
  const trimmed = text.trim()
  if (!/^\d{1,10}$/.test(trimmed)) return null
  const value = Number(trimmed)
  return value <= (type === 'set_radio_power' ? RADIO_POWER_MAX : COUNT_MAX) ? value : null
}

export function commandArguments(type: CommandType, value: number): CommandArguments {
  return type === 'set_radio_power' ? { power_level: value } : { count: value }
}

export function commandValue(command: Pick<NodeCommand, 'arguments'>): number | undefined {
  return command.arguments.power_level ?? command.arguments.count
}
