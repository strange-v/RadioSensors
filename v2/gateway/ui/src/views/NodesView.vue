<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, reactive, ref } from 'vue'
import { useI18n } from 'vue-i18n'
import { useRoute, useRouter } from 'vue-router'
import { api, errorCode } from '../api/client'
import type { GatewayNode, Health } from '../api/types'
import HexBlockInput from '../components/HexBlockInput.vue'
import Icon from '../components/Icon.vue'
import Modal from '../components/Modal.vue'
import { PAIRING_KEY_HEX_LENGTH, PAIRING_UID_HEX_LENGTH, parsePairingPaste } from '../utils/hexCredentials'
import { lastSeen, signal } from '../utils/time'

const route = useRoute()
const router = useRouter()
const { t } = useI18n()
const nodes = ref<GatewayNode[]>([])
const health = ref<Health | null>(null)
const loading = ref(true)
const registryUnavailable = ref(false)
const failure = ref('')
const showPairing = ref(route.query.pair === '1')
const pairingFailure = ref('')
const pairingActive = ref(false)
const pairingRemaining = ref(0)
const pairingBusy = ref(false)
const credentials = reactive({ uid: '', factoryKey: '' })
const uidInput = ref<InstanceType<typeof HexBlockInput> | null>(null)
const keyInput = ref<InstanceType<typeof HexBlockInput> | null>(null)
const invalidCredentials = computed(() => credentials.uid.length !== PAIRING_UID_HEX_LENGTH || credentials.factoryKey.length !== PAIRING_KEY_HEX_LENGTH)
let pairingTimer: number | undefined

async function load() {
  loading.value = true
  const [registryResult, healthResult] = await Promise.allSettled([api.nodes(), api.health()])
  if (registryResult.status === 'fulfilled') nodes.value = registryResult.value.nodes
  else registryUnavailable.value = true
  if (healthResult.status === 'fulfilled') { health.value = healthResult.value; pairingActive.value = healthResult.value.pairing.active; pairingRemaining.value = healthResult.value.pairing.remaining_seconds }
  else failure.value = errorCode(healthResult.reason)
  loading.value = false
}

function openPairing() {
  pairingFailure.value = ''
  credentials.uid = ''
  credentials.factoryKey = ''
  showPairing.value = true
  router.replace({ query: { pair: '1' } })
}

// A full JSON/text paste (from the printed pairing label or QR export) can
// fill both fields in one action instead of forcing two separate pastes.
// Runs in the capture phase so it claims the event before the focused
// block's own fallback (sequential-fill) paste handler sees it.
function handleCredentialsPaste(event: ClipboardEvent) {
  const parsed = parsePairingPaste(event.clipboardData?.getData('text') ?? '')
  if (!parsed.uid && !parsed.key) return
  event.preventDefault()
  event.stopPropagation()
  if (parsed.uid) credentials.uid = parsed.uid
  if (parsed.key) credentials.factoryKey = parsed.key
  if (parsed.uid?.length === PAIRING_UID_HEX_LENGTH && !parsed.key) keyInput.value?.focusFirst()
}

async function closePairing() {
  if (pairingActive.value) { try { await api.closePairing() } catch (error) { pairingFailure.value = errorCode(error); return } }
  showPairing.value = false
  pairingActive.value = false
  router.replace({ query: {} })
}

async function beginPairing() {
  pairingBusy.value = true
  pairingFailure.value = ''
  try { const state = await api.openPairing(credentials.uid, credentials.factoryKey); credentials.factoryKey = ''; pairingActive.value = state.active; pairingRemaining.value = state.remaining_seconds }
  catch (error) { pairingFailure.value = errorCode(error) }
  finally { pairingBusy.value = false }
}

async function refreshPairing() {
  if (!pairingActive.value) return
  try { const state = await api.health(); pairingActive.value = state.pairing.active; pairingRemaining.value = state.pairing.remaining_seconds; if (!state.pairing.active) await load() } catch { /* Keep the last visible state. */ }
}
onMounted(() => { load(); pairingTimer = window.setInterval(refreshPairing, 1000) })
onBeforeUnmount(() => window.clearInterval(pairingTimer))
</script>

<template>
  <div class="page">
    <div class="page-heading"><div><h1>{{ $t('nodes.title') }}</h1><p>{{ $t('nodes.subtitle') }}</p></div><button class="button primary" type="button" @click="openPairing"><Icon name="plus" /> {{ $t('nodes.add') }}</button></div>

    <div v-if="loading" class="empty-state panel"><span class="loader"></span><p>{{ $t('common.loading') }}</p></div>
    <div v-else-if="failure && !health" class="empty-state panel"><span class="state-icon warning"><Icon name="alert" /></span><h2>{{ $t('error.title') }}</h2><p>{{ $t(`error.${failure}`) }}</p></div>
    <template v-else>
      <div class="summary-strip standalone-summary">
        <div><span>{{ $t('nodes.registered') }}</span><strong>{{ health?.registry.records ?? nodes.length }}</strong></div>
        <div><span>{{ $t('nodes.seen') }}</span><strong>{{ health?.telemetry.nodes_seen ?? '—' }}</strong></div>
        <div><span>{{ $t('nodes.updates') }}</span><strong>{{ health?.telemetry.updates ?? '—' }}</strong></div>
      </div>

      <section v-if="nodes.length" class="panel nodes-panel">
        <header class="panel-heading"><div><h2>{{ $t('nodes.title') }}</h2><p>{{ $t('overview.nodesHint') }}</p></div><span class="panel-count">{{ nodes.length }}</span></header>
        <div class="node-list">
          <div v-for="node in nodes" :key="node.node_id" class="node-row">
            <span class="node-symbol" aria-hidden="true"><Icon name="access-point" /></span>
            <span class="node-name"><strong>{{ node.name || $t('nodes.unnamed', { id: node.node_id }) }}</strong><small>{{ $t('nodes.profile', { id: node.profile_id }) }} · ID {{ node.node_id }}</small></span>
            <span class="node-seen"><strong>{{ lastSeen(t, node.last_seen_at_ms) }}</strong><small>{{ signal(node.rssi) }}</small></span>
            <span class="inline-status" :class="{ warning: node.state !== 'active' }">{{ $t(`nodes.state.${node.state}`) }}</span>
          </div>
        </div>
      </section>
      <section v-else class="panel panel-message large">
        <span class="large-symbol" aria-hidden="true"><Icon name="access-point" /></span>
        <h2>{{ registryUnavailable ? $t('nodes.registryUnavailableTitle') : $t('nodes.emptyTitle') }}</h2>
        <p>{{ registryUnavailable ? $t('nodes.registryUnavailable') : $t('nodes.emptyHint') }}</p>
        <button class="button primary" type="button" @click="openPairing"><Icon name="plus" /> {{ $t('nodes.addFirst') }}</button>
      </section>
    </template>

    <Modal v-if="showPairing" :title="$t('pairing.title')" @close="closePairing">
      <div class="form-stack">
        <template v-if="!pairingActive">
          <div class="field-group" @paste.capture="handleCredentialsPaste">
            <div class="field">
              <span>{{ $t('pairing.uid') }}</span>
              <HexBlockInput ref="uidInput" v-model="credentials.uid" :length="20" :label="$t('pairing.uid')" @complete="keyInput?.focusFirst()" />
            </div>
            <div class="field">
              <span>{{ $t('pairing.key') }}</span>
              <HexBlockInput ref="keyInput" v-model="credentials.factoryKey" :length="32" :label="$t('pairing.key')" />
            </div>
          </div>
          <p class="pairing-instructions">{{ $t('pairing.instructions') }}</p>
        </template>
        <div v-if="pairingActive" class="physical-status open"><span class="pulse" aria-hidden="true"></span><p>{{ $t('pairing.active', { seconds: pairingRemaining }) }}</p></div>
        <div v-if="pairingFailure" class="notice error">{{ $t(`error.${pairingFailure}`) }}</div>
        <div class="modal-actions"><button class="button secondary" type="button" @click="closePairing">{{ pairingActive ? $t('pairing.stop') : $t('common.cancel') }}</button><button v-if="!pairingActive" class="button primary" :disabled="pairingBusy || invalidCredentials" type="button" @click="beginPairing">{{ pairingBusy ? $t('pairing.starting') : $t('pairing.start') }}</button></div>
      </div>
    </Modal>
  </div>
</template>
