<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, reactive, ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { api, errorCode } from '../api/client'
import type { GatewayNode, Health } from '../api/types'

const route = useRoute()
const router = useRouter()
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
const normalizedUid = computed(() => credentials.uid.replace(/[-:\s]/g, ''))
const normalizedKey = computed(() => credentials.factoryKey.replace(/[-:\s]/g, ''))
const invalidCredentials = computed(() => !/^[0-9a-fA-F]{20}$/.test(normalizedUid.value) || !/^[0-9a-fA-F]{32}$/.test(normalizedKey.value))
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
  showPairing.value = true
  router.replace({ query: { pair: '1' } })
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
  try { const state = await api.openPairing(normalizedUid.value, normalizedKey.value); credentials.factoryKey = ''; pairingActive.value = state.active; pairingRemaining.value = state.remaining_seconds }
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
    <div class="page-heading"><div><h1>{{ $t('nodes.title') }}</h1><p>{{ $t('nodes.subtitle') }}</p></div><button class="button primary" type="button" @click="openPairing">＋ {{ $t('nodes.add') }}</button></div>

    <div v-if="loading" class="empty-state panel"><span class="loader"></span><p>{{ $t('common.loading') }}</p></div>
    <div v-else-if="failure && !health" class="empty-state panel"><span class="state-icon warning">!</span><h2>{{ $t('error.title') }}</h2><p>{{ $t(`error.${failure}`) }}</p></div>
    <template v-else>
      <div class="summary-strip standalone-summary">
        <div><span>{{ $t('nodes.registered') }}</span><strong>{{ health?.registry.records ?? nodes.length }}</strong></div>
        <div><span>{{ $t('nodes.seen') }}</span><strong>{{ health?.telemetry.nodes_seen ?? '—' }}</strong></div>
        <div><span>{{ $t('nodes.updates') }}</span><strong>{{ health?.telemetry.updates ?? '—' }}</strong></div>
      </div>

      <section v-if="nodes.length" class="panel">
        <div class="node-list">
          <div v-for="node in nodes" :key="node.node_id" class="node-row">
            <span class="node-symbol" aria-hidden="true">◉</span>
            <span class="node-name"><strong>{{ node.name || $t('nodes.unnamed', { id: node.node_id }) }}</strong><small>{{ $t('nodes.profile', { id: node.profile_id }) }} · ID {{ node.node_id }}</small></span>
            <span class="node-seen"><strong>{{ node.last_seen_at_ms ? new Date(node.last_seen_at_ms).toLocaleString() : '—' }}</strong><small>{{ node.rssi === undefined ? '—' : `${node.rssi} dBm` }}</small></span>
            <span class="inline-status" :class="{ warning: node.state !== 'active' }">{{ node.state }}</span>
          </div>
        </div>
      </section>
      <section v-else class="panel panel-message large">
        <span class="large-symbol" aria-hidden="true">◉</span>
        <h2>{{ registryUnavailable ? $t('nodes.registryUnavailableTitle') : $t('nodes.emptyTitle') }}</h2>
        <p>{{ registryUnavailable ? $t('nodes.registryUnavailable') : $t('nodes.emptyHint') }}</p>
        <button class="button primary" type="button" @click="openPairing">{{ $t('nodes.addFirst') }}</button>
      </section>
    </template>

    <div v-if="showPairing" class="modal-backdrop" @click.self="closePairing">
      <section class="modal" role="dialog" aria-modal="true" :aria-label="$t('pairing.title')">
        <header class="modal-heading"><div><h2>{{ $t('pairing.title') }}</h2><p>{{ $t('pairing.intro') }}</p></div><button class="icon-button" type="button" :aria-label="$t('common.close')" @click="closePairing">×</button></header>
        <div class="form-stack">
          <template v-if="!pairingActive"><label><span>{{ $t('pairing.uid') }}</span><input v-model.trim="credentials.uid" autocomplete="off" :placeholder="$t('pairing.uidPlaceholder')"><small>{{ $t('pairing.uidHint') }}</small></label><label><span>{{ $t('pairing.key') }}</span><input v-model.trim="credentials.factoryKey" type="password" autocomplete="off" :placeholder="$t('pairing.keyPlaceholder')"><small>{{ $t('pairing.keyHint') }}</small></label></template>
          <div v-if="pairingActive" class="physical-status open"><span class="pulse" aria-hidden="true"></span><div><strong>{{ $t('pairing.activeTitle') }}</strong><p>{{ $t('pairing.active', { seconds: pairingRemaining }) }}</p></div></div>
          <div v-else class="physical-status"><span class="pulse" aria-hidden="true"></span><div><strong>{{ $t('pairing.readyTitle') }}</strong><p>{{ $t('pairing.ready') }}</p></div></div>
          <div v-if="pairingFailure" class="notice error">{{ $t(`error.${pairingFailure}`) }}</div>
          <div class="modal-actions"><button class="button secondary" type="button" @click="closePairing">{{ pairingActive ? $t('pairing.stop') : $t('common.cancel') }}</button><button v-if="!pairingActive" class="button primary" :disabled="pairingBusy || invalidCredentials" type="button" @click="beginPairing">{{ pairingBusy ? $t('pairing.starting') : $t('pairing.start') }}</button></div>
        </div>
      </section>
    </div>
  </div>
</template>
