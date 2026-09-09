import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..')
const readJson = (name) => JSON.parse(readFileSync(resolve(root, name), 'utf8'))

const manifest = readJson('protocol-manifest.json')
const schema = readJson('protocol-manifest.schema.json')
const vectors = readJson('protocol-vectors.json')

assert.equal(manifest.schema_version, 1)
assert.equal(vectors.schema_version, manifest.schema_version)
assert.equal(schema.$schema, 'https://json-schema.org/draft/2020-12/schema')

const integerTypes = {
  uint8: { bytes: 1, min: 0, max: 0xff, read: (buffer, offset) => buffer.readUInt8(offset) },
  int16_le: { bytes: 2, min: -0x8000, max: 0x7fff, read: (buffer, offset) => buffer.readInt16LE(offset) },
  uint16_le: { bytes: 2, min: 0, max: 0xffff, read: (buffer, offset) => buffer.readUInt16LE(offset) },
  uint32_le: { bytes: 4, min: 0, max: 0xffffffff, read: (buffer, offset) => buffer.readUInt32LE(offset) },
  uint64_le: {
    bytes: 8,
    min: 0n,
    max: 0xffffffffffffffffn,
    read: (buffer, offset) => buffer.readBigUInt64LE(offset),
  },
}

function unique(values, label) {
  assert.equal(new Set(values).size, values.length, `${label} must be unique`)
}

function byteLength(field, fieldsByName = new Map()) {
  if (field.encoding !== 'bytes') return integerTypes[field.encoding].bytes
  if (field.length !== undefined) return field.length
  const source = fieldsByName.get(field.length_from)
  assert(source, `${field.name} references unknown length field ${field.length_from}`)
  return source
}

function typeSentinel(field) {
  if (field.no_value_raw === undefined) return undefined
  if (typeof field.no_value_raw === 'number') return field.no_value_raw
  const type = integerTypes[field.encoding]
  assert(type, `${field.name} cannot use a type sentinel with ${field.encoding}`)
  return field.no_value_raw === 'type_min' ? type.min : type.max
}

function validateField(field) {
  if (field.encoding === 'bytes') return
  const type = integerTypes[field.encoding]
  assert(type, `unsupported encoding ${field.encoding}`)
  for (const value of field.allowed_raw ?? []) {
    assert(value >= type.min && value <= type.max, `${field.name} allowed value exceeds its type`)
  }
  if (field.valid_raw) {
    assert(field.valid_raw.min <= field.valid_raw.max, `${field.name} has an inverted range`)
    assert(field.valid_raw.min >= type.min && field.valid_raw.max <= type.max,
      `${field.name} range exceeds its type`)
  }
  const sentinel = typeSentinel(field)
  if (sentinel !== undefined) {
    assert(sentinel >= type.min && sentinel <= type.max, `${field.name} sentinel exceeds its type`)
  }
}

function validateLayout(fields, size, reservedPrefix = 0) {
  const occupied = new Set(Array.from({ length: reservedPrefix }, (_, index) => index))
  unique(fields.map((field) => field.name), 'field names')
  for (const field of fields) {
    validateField(field)
    const length = field.encoding === 'bytes' && field.length_from !== undefined
      ? 0
      : byteLength(field)
    assert(field.offset + length <= size, `${field.name} exceeds frame size`)
    for (let offset = field.offset; offset < field.offset + length; ++offset) {
      assert(!occupied.has(offset), `${field.name} overlaps byte ${offset}`)
      occupied.add(offset)
    }
  }
}

function readField(buffer, field, decodedByName) {
  if (field.encoding === 'bytes') {
    const length = field.length ?? decodedByName[field.length_from]
    const value = buffer.subarray(field.offset, field.offset + length).toString('hex')
    return field.format === 'lowercase_hex' ? value : value.toUpperCase()
  }
  const value = integerTypes[field.encoding].read(buffer, field.offset)
  if (typeof value === 'bigint') {
    return value <= BigInt(Number.MAX_SAFE_INTEGER) ? Number(value) : value.toString()
  }
  return value
}

function fieldValue(raw, field) {
  if (raw === typeSentinel(field)) return null
  if (field.allowed_raw) assert(field.allowed_raw.includes(raw), `${field.name} has an invalid value`)
  if (field.valid_raw) {
    assert(raw >= field.valid_raw.min && raw <= field.valid_raw.max,
      `${field.name} is outside its valid range`)
  }
  return field.scale ? raw * field.scale.numerator / field.scale.denominator : raw
}

const telemetry = manifest.telemetry
const profiles = new Map(telemetry.profiles.map((profile) => [profile.id, profile]))
unique([...profiles.keys()], 'profile IDs')
unique(telemetry.profiles.map((profile) => profile.name), 'profile names')
for (const profile of telemetry.profiles) {
  validateLayout([...telemetry.common_fields, ...profile.fields], profile.frame_size, 1)
}

function decodeTelemetry(vector) {
  const profile = profiles.get(vector.profile_id)
  assert(profile, `unknown profile ${vector.profile_id}`)
  const buffer = Buffer.from(vector.hex, 'hex')
  if (buffer[0] !== telemetry.header) throw new Error('wrong_header')
  if (buffer.length !== profile.frame_size) throw new Error('wrong_length')
  const raw = {}
  const values = {}
  try {
    for (const field of [...telemetry.common_fields, ...profile.fields]) {
      raw[field.name] = readField(buffer, field, raw)
      values[field.name] = fieldValue(raw[field.name], field)
    }
  } catch {
    throw new Error('invalid_value')
  }
  return { raw, values }
}

for (const vector of vectors.telemetry) {
  const decoded = decodeTelemetry(vector)
  assert.deepEqual(decoded.raw, vector.raw, vector.name)
  assert.deepEqual(decoded.values, vector.values, vector.name)
}
for (const vector of vectors.invalid_telemetry) {
  assert.throws(() => decodeTelemetry(vector), { message: vector.error }, vector.name)
}
for (const profile of telemetry.profiles) {
  assert(vectors.telemetry.some((vector) => vector.profile_id === profile.id),
    `profile ${profile.id} has no golden vector`)
}

const stream = manifest.gateway_stream
const messages = new Map(stream.messages.map((message) => [message.kind, message]))
unique([...messages.keys()], 'stream message kinds')
unique(stream.messages.map((message) => message.name), 'stream message names')
for (const message of stream.messages) {
  const minimumSize = message.size.fixed ?? message.size.base
  validateLayout([...stream.common_fields, ...message.fields], minimumSize)
}

function decodeStream(vector) {
  const buffer = Buffer.from(vector.hex, 'hex')
  const kind = buffer[1]
  const message = messages.get(kind)
  assert(message, `unknown stream message kind ${kind}`)
  assert.equal(message.name, vector.message, `${vector.name} names the wrong message`)
  const decoded = {}
  for (const field of [...stream.common_fields, ...message.fields]) {
    decoded[field.name] = readField(buffer, field, decoded)
  }
  const expectedSize = message.size.fixed ?? message.size.base + decoded[message.size.plus_field]
  assert.equal(buffer.length, expectedSize, `${vector.name} has the wrong size`)
  return decoded
}

for (const vector of vectors.gateway_stream) {
  assert.deepEqual(decodeStream(vector), vector.decoded, vector.name)
}
for (const message of stream.messages) {
  assert(vectors.gateway_stream.some((vector) => vector.message === message.name),
    `${message.name} has no golden vector`)
}

console.log(`Validated ${profiles.size} profiles, ${vectors.telemetry.length} telemetry vectors, and ${messages.size} stream messages.`)
