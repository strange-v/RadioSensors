<script setup lang="ts">
// The node's own report of its radio next to the gateway's policy for it.
// Power is desired state: the gateway asks for a level in its
// acknowledgements and the node reports the level it actually uses, so the
// card shows both and never presents the wanted level as the real one.
import { computed, ref, watch } from 'vue'
import { api, errorCode, isAdmin } from '../api/client'
import type { GatewayNode, PowerPolicy } from '../api/types'
import { signal } from '../utils/format'
import { parsePowerLevel } from '../utils/radio'

const props = defineProps<{ node: GatewayNode }>()
const emit = defineEmits<{ updated: [] }>()

const ceiling = computed(() => props.node.max_power_level ?? 0)
const initialLevel = (node: GatewayNode) => String(node.fixed_power_level ?? node.tx_power_level ?? node.max_power_level ?? 0)
const mode = ref<PowerPolicy>(props.node.power_policy ?? 'auto')
const draftLevel = ref(initialLevel(props.node))
const saving = ref(false)
const failure = ref('')

const parsedLevel = computed(() => parsePowerLevel(draftLevel.value, ceiling.value))
const invalid = computed(() => mode.value === 'fixed' && parsedLevel.value === null)
const unchanged = computed(() => mode.value === (props.node.power_policy ?? 'auto') &&
  (mode.value === 'auto' || parsedLevel.value === props.node.fixed_power_level))
const adjusting = computed(() => props.node.tx_power_target !== undefined &&
  props.node.tx_power_level !== undefined && props.node.tx_power_target !== props.node.tx_power_level)

watch(() => props.node, (node) => {
  mode.value = node.power_policy ?? 'auto'
  draftLevel.value = initialLevel(node)
  failure.value = ''
})

async function save() {
  const level = parsedLevel.value
  if (invalid.value || unchanged.value || saving.value) return
  saving.value = true
  failure.value = ''
  try {
    await api.setPowerPolicy(props.node.node_id, mode.value === 'fixed' && level !== null
      ? { power_policy: 'fixed', fixed_power_level: level }
      : { power_policy: 'auto' })
    emit('updated')
  } catch (error) {
    failure.value = errorCode(error)
  } finally {
    saving.value = false
  }
}
</script>

<template>
  <section class="form-stack node-radio">
    <h3>{{ $t('radio.title') }}</h3>
    <dl class="simple-details">
      <div><dt>{{ $t('radio.level') }}</dt><dd>
        <template v-if="node.tx_power_level !== undefined">{{ $t('radio.levelValue', { level: node.tx_power_level, max: ceiling }) }}</template>
        <span v-else class="muted">{{ $t('radio.noReport') }}</span>
      </dd></div>
      <div v-if="adjusting" class="radio-target"><dt>{{ $t('radio.target') }}</dt><dd>
        {{ node.tx_power_target }}<small>{{ $t('radio.targetHint') }}</small>
      </dd></div>
      <div><dt>{{ $t('radio.uplink') }}</dt><dd>{{ signal(node.rssi) }}</dd></div>
      <div><dt>{{ $t('radio.downlink') }}</dt><dd>{{ signal(node.downlink_rssi) }}</dd></div>
    </dl>
    <div v-if="node.radio_fallback" class="notice radio-fallback">{{ $t('radio.fallback') }}</div>
    <div v-if="node.supply_limited" class="notice radio-supply">{{ $t('radio.supplyLimited') }}</div>

    <template v-if="isAdmin">
      <div class="segmented" role="group" :aria-label="$t('radio.policy')">
        <button type="button" :class="{ active: mode === 'auto' }" @click="mode = 'auto'">{{ $t('radio.auto') }}</button>
        <button type="button" :class="{ active: mode === 'fixed' }" @click="mode = 'fixed'">{{ $t('radio.fixed') }}</button>
      </div>
      <label v-if="mode === 'fixed'">
        <span>{{ $t('radio.fixedLevel') }}</span>
        <input v-model="draftLevel" inputmode="numeric" :aria-label="$t('radio.fixedLevel')" @keydown.enter="save">
        <small :class="{ invalid }">{{ $t('radio.ceilingHint', { max: ceiling }) }}</small>
      </label>
      <p v-else class="radio-hint">{{ $t('radio.autoHint') }}</p>
      <div v-if="failure" class="notice error">{{ $t(`error.${failure}`) }}</div>
      <div class="radio-actions">
        <button class="button primary" :disabled="saving || invalid || unchanged" type="button" @click="save">{{ saving ? $t('radio.saving') : $t('radio.save') }}</button>
      </div>
    </template>
    <p v-else class="radio-hint">{{ node.power_policy === 'fixed' ? $t('radio.fixedValue', { level: node.fixed_power_level }) : $t('radio.autoValue') }}</p>
  </section>
</template>

<style scoped>
.node-radio { padding-top: var(--space-4); border-top: 1px solid var(--line); }
.node-radio h3 { font-size: var(--text-md); }
.node-radio .segmented { justify-self: start; }
.radio-target small { display: block; color: var(--muted); font-size: var(--text-sm); font-weight: 400; }
.radio-hint { color: var(--ink-soft); font-size: var(--text-sm); }
.radio-actions { display: flex; justify-content: flex-end; }
.muted { color: var(--muted); }
</style>
