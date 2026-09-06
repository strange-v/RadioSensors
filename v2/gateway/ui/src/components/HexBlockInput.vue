<script setup lang="ts">
// Segmented hex-code input: splits a fixed-length hex value into 4-character
// blocks, filters/normalizes keystrokes and pastes, and auto-advances focus.
// Used for the pairing device UID (20 hex chars) and factory key (32 hex
// chars) fields in NodesView, which are too long and error-prone as a single
// free-text input. See ROADMAP.md pairing UX notes.
import { computed, nextTick, reactive, watch } from 'vue'
import { normalizeHexFragment } from '../utils/hexCredentials'

const props = withDefaults(defineProps<{
  modelValue: string
  length: number
  blockSize?: number
  label: string
  masked?: boolean
}>(), { blockSize: 4, masked: false })

const emit = defineEmits<{ 'update:modelValue': [value: string]; complete: [] }>()

const blockCount = computed(() => Math.ceil(props.length / props.blockSize))

function splitValue(value: string): string[] {
  const clean = value.slice(0, props.length)
  const result: string[] = []
  for (let index = 0; index < blockCount.value; index++) {
    result.push(clean.slice(index * props.blockSize, index * props.blockSize + props.blockSize))
  }
  return result
}

const blocks = reactive<string[]>(splitValue(props.modelValue))
const inputs: (HTMLInputElement | null)[] = []

watch(() => props.modelValue, (value) => {
  splitValue(value).forEach((chunk, index) => { blocks[index] = chunk })
})

function currentValue(): string {
  return blocks.join('').slice(0, props.length)
}

function setInputRef(el: unknown, index: number) {
  inputs[index] = (el as HTMLInputElement | null) ?? null
}

function focusBlock(index: number) {
  void nextTick(() => inputs[index]?.focus())
}

defineExpose({ focusFirst: () => focusBlock(0) })

function onInput(index: number, event: Event) {
  const target = event.target as HTMLInputElement
  const clipped = normalizeHexFragment(target.value).slice(0, props.blockSize)
  target.value = clipped
  blocks[index] = clipped
  emit('update:modelValue', currentValue())
  if (clipped.length === props.blockSize) {
    if (index < blockCount.value - 1) focusBlock(index + 1)
    else emit('complete')
  }
}

function onKeydown(index: number, event: KeyboardEvent) {
  if (event.key === 'Backspace' && !blocks[index] && index > 0) {
    focusBlock(index - 1)
  } else if (event.key === 'ArrowLeft' && index > 0) {
    focusBlock(index - 1)
  } else if (event.key === 'ArrowRight' && index < blockCount.value - 1) {
    focusBlock(index + 1)
  }
}

// Sequential fallback for a paste landing on a single block. A whole-value
// paste (JSON, both credentials, distinguishable lengths) is intercepted
// earlier by the pairing form's capture-phase handler; this only runs when
// that handler declines to act.
function onPaste(index: number, event: ClipboardEvent) {
  const clean = normalizeHexFragment(event.clipboardData?.getData('text') ?? '')
  if (!clean) return
  event.preventDefault()
  let cursor = index
  let remaining = clean
  while (remaining.length > 0 && cursor < blockCount.value) {
    const chunk = remaining.slice(0, props.blockSize)
    blocks[cursor] = chunk
    if (inputs[cursor]) inputs[cursor]!.value = chunk
    remaining = remaining.slice(props.blockSize)
    cursor++
  }
  emit('update:modelValue', currentValue())
  focusBlock(Math.min(cursor, blockCount.value - 1))
}
</script>

<template>
  <div class="hex-block-input" role="group" :aria-label="label">
    <template v-for="index in blockCount" :key="index - 1">
      <span v-if="index > 1" class="hex-block-separator" aria-hidden="true">-</span>
      <input
        :ref="(el) => setInputRef(el, index - 1)"
        class="hex-block"
        :type="masked ? 'password' : 'text'"
        :value="blocks[index - 1]"
        :maxlength="blockSize"
        inputmode="text"
        autocomplete="off"
        autocorrect="off"
        autocapitalize="characters"
        spellcheck="false"
        :aria-label="`${label} ${index}/${blockCount}`"
        @input="onInput(index - 1, $event)"
        @keydown="onKeydown(index - 1, $event)"
        @paste="onPaste(index - 1, $event)"
      >
    </template>
  </div>
</template>

<style scoped>
.hex-block-input { display: flex; flex-wrap: wrap; align-items: center; gap: var(--space-2); }
.hex-block {
  width: 4.4em; min-height: 38px; padding: var(--space-2) 0; border: 1px solid var(--line-strong); border-radius: var(--radius-md);
  color: var(--ink); background: var(--bg); font-family: var(--font-mono);
  font-size: var(--text-lg); letter-spacing: .08em; text-align: center; text-transform: uppercase;
}
.hex-block:focus { border-color: var(--primary); box-shadow: 0 0 0 3px var(--focus-ring); outline: none; }
.hex-block-separator { color: var(--muted); font-weight: 600; }
</style>
