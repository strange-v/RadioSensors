import type { CommandArguments, CommandType, NodeCommand } from '../api/types'

// Mirrors the per-profile `commands` lists in protocol/protocol-manifest.json;
// commands.test.ts holds the two together. The gateway refuses anything else
// with `unsupported_command`, so this only decides what the form offers.
const PROFILE_COMMANDS: Record<number, CommandType[]> = {
  1: ['read_info'],
  2: ['read_info'],
  3: ['read_info'],
  4: ['read_info'],
  5: ['read_info'],
  6: ['set_count', 'read_info'],
  7: ['read_info'],
  8: ['read_info'],
}

export const COUNT_MAX = 0xffff_ffff

export function supportedCommands(profileId: number): CommandType[] {
  return PROFILE_COMMANDS[profileId] ?? []
}

export function commandTakesValue(type: CommandType): boolean {
  return type === 'set_count'
}

// Whole decimal numbers only: "1e3" or "12.5" would reach the node as
// something other than what was typed.
export function parseCommandValue(_type: CommandType, text: string): number | null {
  const trimmed = text.trim()
  if (!/^\d{1,10}$/.test(trimmed)) return null
  const value = Number(trimmed)
  return value <= COUNT_MAX ? value : null
}

export function commandArguments(type: CommandType, value: number | null): CommandArguments {
  return commandTakesValue(type) && value !== null ? { count: value } : {}
}

export function commandValue(command: Pick<NodeCommand, 'arguments'>): number | undefined {
  return command.arguments.count
}
