<script setup lang="ts">
// Everything known about one node, plus the two mutations the registry
// supports. It exists as a dialog rather than a row menu because device_uid
// has nowhere to live in the table, and renaming needs a validated form.
import { computed, ref, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import { api, errorCode, isAdmin } from '../api/client'
import type { GatewayNode } from '../api/types'
import Modal from './Modal.vue'
import SignalBars from './SignalBars.vue'
import { byteLength, lastSeen, nodeName, signal } from '../utils/format'

const props = defineProps<{ node: GatewayNode }>()
const emit = defineEmits<{ close: []; changed: [] }>()

const { t } = useI18n()
const DISPLAY_NAME_MAX_BYTES = 48

const draftName = ref(props.node.display_name)
const saving = ref(false)
const deleting = ref(false)
const confirmingDelete = ref(false)
const failure = ref('')

const usedBytes = computed(() => byteLength(draftName.value))
// The API accepts an empty name (it clears one), so only the ceiling and
// control characters make a draft invalid.
const controlCharacters = /\p{Cc}/u
const invalidName = computed(() => usedBytes.value > DISPLAY_NAME_MAX_BYTES || controlCharacters.test(draftName.value))
const unchanged = computed(() => draftName.value === props.node.display_name)

watch(() => props.node, (node) => {
  draftName.value = node.display_name
  confirmingDelete.value = false
  failure.value = ''
})

async function rename() {
  if (invalidName.value || unchanged.value || saving.value) return
  saving.value = true
  failure.value = ''
  try {
    await api.renameNode(props.node.node_id, draftName.value)
    emit('changed')
    emit('close')
  } catch (error) {
    failure.value = errorCode(error)
  } finally {
    saving.value = false
  }
}

async function remove() {
  deleting.value = true
  failure.value = ''
  try {
    await api.deleteNode(props.node.node_id)
    emit('changed')
    emit('close')
  } catch (error) {
    failure.value = errorCode(error)
    confirmingDelete.value = false
  } finally {
    deleting.value = false
  }
}
</script>

<template>
  <Modal :title="nodeName(t, node)" @close="emit('close')">
    <div class="form-stack">
      <dl class="simple-details">
        <div><dt>{{ $t('nodes.deviceUid') }}</dt><dd class="node-uid">{{ node.device_uid }}</dd></div>
        <div><dt>{{ $t('nodes.columnState') }}</dt><dd><span class="inline-status" :class="{ warning: node.state !== 'active' }">{{ $t(`nodes.state.${node.state}`) }}</span></dd></div>
        <div><dt>ID</dt><dd>{{ node.node_id }}</dd></div>
        <div><dt>{{ $t('nodes.profileLabel') }}</dt><dd>{{ node.profile_id }}</dd></div>
        <div><dt>{{ $t('nodes.columnFirmware') }}</dt><dd>{{ node.firmware || '—' }}</dd></div>
        <div><dt>{{ $t('nodes.columnSignal') }}</dt><dd>
          <span v-if="node.has_telemetry === false" class="muted">{{ $t('nodes.noTelemetry') }}</span>
          <span v-else class="node-signal"><SignalBars :rssi="node.rssi" />{{ signal(node.rssi) }}</span>
        </dd></div>
        <div><dt>{{ $t('nodes.columnLastSeen') }}</dt><dd>{{ lastSeen(t, node.last_seen_at_ms) }}</dd></div>
      </dl>

      <template v-if="isAdmin">
        <label>
          <span>{{ $t('nodes.displayName') }}</span>
          <input v-model="draftName" :maxlength="DISPLAY_NAME_MAX_BYTES" :aria-label="$t('nodes.displayName')">
          <small :class="{ invalid: invalidName }">{{ $t('nodes.displayNameHint', { used: usedBytes, max: DISPLAY_NAME_MAX_BYTES }) }}</small>
          <small v-if="!draftName">{{ $t('nodes.displayNameEmpty', { fallback: $t('nodes.unnamed', { id: node.node_id }) }) }}</small>
        </label>

        <div v-if="failure" class="notice error">{{ $t(`error.${failure}`) }}</div>

        <div v-if="confirmingDelete" class="notice error confirm-delete">
          <span>
            <strong>{{ $t('nodes.deleteConfirmTitle', { name: nodeName(t, node) }) }}</strong>
            {{ $t('nodes.deleteConfirmHint') }}
          </span>
        </div>

        <div class="modal-actions">
          <template v-if="confirmingDelete">
            <button class="button secondary" type="button" @click="confirmingDelete = false">{{ $t('common.cancel') }}</button>
            <button class="button danger" :disabled="deleting" type="button" @click="remove">{{ deleting ? $t('nodes.deleting') : $t('nodes.deleteConfirm') }}</button>
          </template>
          <template v-else>
            <button class="button danger-text" type="button" @click="confirmingDelete = true">{{ $t('nodes.delete') }}</button>
            <button class="button primary" :disabled="saving || invalidName || unchanged" type="button" @click="rename">{{ saving ? $t('nodes.saving') : $t('nodes.save') }}</button>
          </template>
        </div>
      </template>
      <p v-else class="coming-soon">{{ $t('nodes.adminOnly') }}</p>
    </div>
  </Modal>
</template>

<style scoped>
.node-uid { overflow-wrap: anywhere; font-family: var(--font-mono); font-size: var(--text-sm); user-select: all; }
.muted { color: var(--muted); }
.node-signal { display: inline-flex; align-items: center; gap: var(--space-2); }
.confirm-delete { margin: 0; }
.confirm-delete strong { display: block; margin-bottom: 2px; }
.modal-actions { justify-content: space-between; }
</style>
