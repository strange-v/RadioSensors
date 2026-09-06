<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import { api, errorCode } from '../api/client'
import type { GatewayInfo, GatewayNode, Health } from '../api/types'

const health = ref<Health | null>(null)
const info = ref<GatewayInfo | null>(null)
const nodes = ref<GatewayNode[]>([])
const nodesAvailable = ref(false)
const failure = ref('')
const refreshing = ref(false)
const updatedAt = ref<Date | null>(null)
let timer: number | undefined

const healthy = computed(() => health.value?.status === 'ok' && health.value?.ethernet.has_ip && health.value?.radio.present && health.value?.storage.ready && health.value?.time.state === 'synced')
const nodeProblem = computed(() => nodes.value.find((node) => node.state !== 'active'))
const needsAttention = computed(() => !healthy.value || Boolean(nodeProblem.value))
const activeNodes = computed(() => nodesAvailable.value ? nodes.value.filter((node) => node.state === 'active').length : health.value?.telemetry.nodes_seen ?? 0)

const fmt = (value: unknown) => value === undefined || value === null || value === '' ? '—' : String(value)
const dateTime = (ms: number | undefined) => ms ? new Intl.DateTimeFormat(undefined, { dateStyle: 'medium', timeStyle: 'short' }).format(ms) : '—'
const ago = (ms: number | undefined) => {
  if (!ms) return '—'
  const minutes = Math.max(0, Math.floor((Date.now() - ms) / 60000))
  if (minutes < 1) return '< 1 min'
  if (minutes < 60) return `${minutes} min`
  const hours = Math.floor(minutes / 60)
  return hours < 24 ? `${hours} h` : `${Math.floor(hours / 24)} d`
}

async function load() {
  refreshing.value = true
  const results = await Promise.allSettled([api.health(), api.info(), api.nodes()])
  if (results[0].status === 'fulfilled') {
    health.value = results[0].value
    failure.value = ''
    updatedAt.value = new Date()
  } else {
    failure.value = errorCode(results[0].reason)
  }
  if (results[1].status === 'fulfilled') info.value = results[1].value
  if (results[2].status === 'fulfilled') {
    nodes.value = results[2].value.nodes
    nodesAvailable.value = true
  }
  refreshing.value = false
}

onMounted(() => {
  load()
  timer = window.setInterval(load, 10000)
})
onBeforeUnmount(() => window.clearInterval(timer))
</script>

<template>
  <div class="page dashboard-page">
    <div class="page-heading">
      <div><h1>{{ $t('overview.title') }}</h1><p>{{ $t('overview.subtitle') }}</p></div>
      <div class="heading-actions">
        <button class="button secondary compact" :disabled="refreshing" type="button" @click="load">↻ {{ $t('common.refresh') }}</button>
        <RouterLink class="button primary" to="/nodes?pair=1">＋ {{ $t('nodes.add') }}</RouterLink>
      </div>
    </div>

    <div v-if="failure && !health" class="empty-state panel">
      <span class="state-icon warning">!</span><h2>{{ $t('error.title') }}</h2><p>{{ $t(`error.${failure}`) }}</p>
      <button class="button primary" type="button" @click="load">{{ $t('common.retry') }}</button>
    </div>

    <template v-else-if="health">
      <div v-if="health.api_version !== 1" class="notice error">{{ $t('status.apiMismatch', { version: health.api_version }) }}</div>
      <div v-if="needsAttention" class="attention-banner">
        <div><span class="attention-icon" aria-hidden="true">!</span><span><strong>{{ $t('overview.attentionTitle') }}</strong><small>{{ nodeProblem ? $t('overview.nodeAttention', { name: nodeProblem.name || `ID ${nodeProblem.node_id}` }) : $t('overview.gatewayAttention') }}</small></span></div>
        <RouterLink class="text-action" :to="nodeProblem ? '/nodes' : '/settings'">{{ $t('common.view') }} →</RouterLink>
      </div>

      <div class="dashboard-grid">
        <section class="panel nodes-panel">
          <header class="panel-heading"><div><h2>{{ $t('nodes.title') }}</h2><p>{{ $t('overview.nodesHint') }}</p></div><RouterLink class="text-action" to="/nodes">{{ $t('common.viewAll') }} →</RouterLink></header>
          <div class="summary-strip">
            <div><span>{{ $t('nodes.registered') }}</span><strong>{{ health.registry.records }}</strong></div>
            <div><span>{{ $t('nodes.seen') }}</span><strong>{{ activeNodes }}</strong></div>
            <div><span>{{ $t('nodes.updates') }}</span><strong>{{ health.telemetry.updates }}</strong></div>
          </div>

          <div v-if="nodesAvailable && nodes.length" class="node-list compact-list">
            <RouterLink v-for="node in nodes.slice(0, 4)" :key="node.node_id" class="node-row" to="/nodes">
              <span class="node-symbol" aria-hidden="true">◉</span>
              <span class="node-name"><strong>{{ node.name || $t('nodes.unnamed', { id: node.node_id }) }}</strong><small>{{ $t('nodes.profile', { id: node.profile_id }) }}</small></span>
              <span class="node-seen"><strong>{{ ago(node.last_seen_at_ms) }}</strong><small>{{ node.rssi === undefined ? '—' : `${node.rssi} dBm` }}</small></span>
              <span class="inline-status" :class="{ warning: node.state !== 'active' }">{{ $t(`nodes.state.${node.state}`) }}</span>
            </RouterLink>
          </div>
          <div v-else class="panel-message"><strong>{{ $t('overview.registrySummary', { count: health.registry.records }) }}</strong><p>{{ $t('overview.registryUnavailable') }}</p></div>
        </section>

        <div class="dashboard-side">
          <section class="panel">
            <header class="panel-heading"><div><h2>{{ $t('overview.gateway') }}</h2><p>{{ $t('overview.gatewayHint') }}</p></div><span class="inline-status" :class="{ warning: !healthy }">{{ healthy ? $t('common.working') : $t('common.attention') }}</span></header>
            <dl class="health-list">
              <div><dt><span aria-hidden="true">↗</span>{{ $t('status.network') }}</dt><dd><strong>{{ health.ethernet.has_ip ? $t('common.connected') : $t('common.disconnected') }}</strong><small>{{ fmt(health.ethernet.ip) }}</small></dd></div>
              <div><dt><span aria-hidden="true">◉</span>{{ $t('status.radio') }}</dt><dd><strong>{{ health.radio.present ? `${(health.radio.frequency_hz / 1e6).toFixed(2)} MHz` : $t('common.unavailable') }}</strong><small>{{ $t('status.networkId') }} {{ health.radio.network_id }}</small></dd></div>
              <div><dt><span aria-hidden="true">◷</span>{{ $t('status.time') }}</dt><dd><strong>{{ health.time.state === 'synced' ? $t('common.synchronized') : health.time.state }}</strong><small>{{ dateTime(health.time.last_sync_ms) }}</small></dd></div>
              <div><dt><span aria-hidden="true">▣</span>{{ $t('status.storage') }}</dt><dd><strong>{{ health.storage.ready ? $t('common.ready') : $t('common.attention') }}</strong><small>{{ $t('status.registryGeneration') }} {{ health.registry.generation }}</small></dd></div>
            </dl>
          </section>

          <section class="panel home-assistant-card">
            <header class="panel-heading"><div><h2>Home Assistant</h2><p>{{ $t('ha.cardHint') }}</p></div><span class="inline-status warning">{{ $t('ha.notConfigured') }}</span></header>
            <div class="integration-row"><span class="integration-symbol" aria-hidden="true">⌂</span><div><strong>{{ $t('ha.connectTitle') }}</strong><p>{{ $t('ha.connectHint') }}</p></div></div>
            <RouterLink class="button primary full" to="/home-assistant">{{ $t('ha.configure') }}</RouterLink>
          </section>
        </div>
      </div>

      <p v-if="updatedAt" class="updated-note">{{ $t('status.updated', { time: updatedAt.toLocaleTimeString() }) }} · {{ $t('status.autoRefresh') }}</p>
      <details class="diagnostics"><summary>{{ $t('common.details') }}</summary><pre>{{ JSON.stringify({ info, health }, null, 2) }}</pre></details>
    </template>
  </div>
</template>
