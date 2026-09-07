<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import { useI18n } from 'vue-i18n'
import { api, errorCode } from '../api/client'
import type { GatewayInfo, GatewayNode, Health } from '../api/types'
import Icon from '../components/Icon.vue'
import SignalBars from '../components/SignalBars.vue'
import { dateTime, lastSeen, megahertz, nodeName, signal, timeOfDay } from '../utils/format'

const { t, locale } = useI18n()
const health = ref<Health | null>(null)
const info = ref<GatewayInfo | null>(null)
const nodes = ref<GatewayNode[]>([])
const nodesAvailable = ref(false)
const failure = ref('')
const refreshing = ref(false)
const updatedAt = ref<Date | null>(null)
let timer: number | undefined

// TimeService reports waiting_for_network | disabled | synchronizing |
// synchronized. Turning NTP off in settings is a deliberate choice, so it does
// not make the gateway unhealthy; the two transient states do, because until
// the clock is set telemetry carries a zero timestamp.
const timeHealthy = computed(() => health.value?.time.state === 'synchronized' || health.value?.time.state === 'disabled')
const healthy = computed(() => health.value?.status === 'ok' && health.value?.ethernet.has_ip && health.value?.radio.present && health.value?.storage.ready && timeHealthy.value)
const nodeProblem = computed(() => nodes.value.find((node) => node.state !== 'active'))
const needsAttention = computed(() => !healthy.value || Boolean(nodeProblem.value))
const activeNodes = computed(() => nodesAvailable.value ? nodes.value.filter((node) => node.state === 'active').length : health.value?.telemetry.nodes_seen ?? 0)

// The widget answers "what needs attention", so it leads with nodes that are
// not active and then with the freshest contacts, rather than showing whichever
// four the registry happened to return first.
const overviewNodes = computed(() => [...nodes.value]
  .sort((left, right) => {
    const problem = Number(right.state !== 'active') - Number(left.state !== 'active')
    return problem !== 0 ? problem : (right.last_seen_at_ms ?? 0) - (left.last_seen_at_ms ?? 0)
  })
  .slice(0, 4))

const fmt = (value: unknown) => value === undefined || value === null || value === '' ? '—' : String(value)

async function load() {
  // A request may now take far longer than the polling interval, so skip a
  // tick rather than stacking calls on a gateway that is already struggling.
  if (refreshing.value) return
  refreshing.value = true
  const results = await Promise.allSettled([api.poll.health(), api.poll.info(), api.poll.nodes()])
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
        <button class="button secondary compact" type="button" @click="load"><Icon name="refresh" /> {{ $t('common.refresh') }}</button>
        <RouterLink class="button primary" to="/nodes?pair=1"><Icon name="plus" /> {{ $t('nodes.add') }}</RouterLink>
      </div>
    </div>

    <div v-if="failure && !health" class="empty-state panel">
      <span class="state-icon warning"><Icon name="alert" /></span><h2>{{ $t('error.title') }}</h2><p>{{ $t(`error.${failure}`) }}</p>
      <button class="button primary" type="button" @click="load">{{ $t('common.retry') }}</button>
    </div>

    <template v-else-if="health">
      <div v-if="health.api_version !== 1" class="notice error">{{ $t('status.apiMismatch', { version: health.api_version }) }}</div>
      <div v-if="needsAttention" class="attention-banner">
        <div><span class="attention-icon" aria-hidden="true"><Icon name="alert" /></span><span><strong>{{ $t('overview.attentionTitle') }}</strong><small>{{ nodeProblem ? $t('overview.nodeAttention', { name: nodeName(t, nodeProblem) }) : $t('overview.gatewayAttention') }}</small></span></div>
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
            <RouterLink v-for="node in overviewNodes" :key="node.node_id" class="node-row" to="/nodes">
              <span class="node-symbol" aria-hidden="true"><Icon name="access-point" /></span>
              <span class="node-name"><strong>{{ nodeName(t, node) }}</strong><small>{{ $t('nodes.profile', { id: node.profile_id }) }}</small></span>
              <span class="node-seen">
                <strong>{{ lastSeen(t, node.last_seen_at_ms) }}</strong>
                <small v-if="node.has_telemetry === false">{{ $t('nodes.noTelemetry') }}</small>
                <small v-else class="node-seen-signal"><SignalBars :rssi="node.rssi" />{{ signal(node.rssi) }}</small>
              </span>
              <span class="inline-status" :class="{ warning: node.state !== 'active' }">{{ $t(`nodes.state.${node.state}`) }}</span>
            </RouterLink>
          </div>
          <div v-else class="panel-message"><strong>{{ $t('overview.registrySummary', { count: health.registry.records }) }}</strong><p>{{ $t('overview.registryUnavailable') }}</p></div>
        </section>

        <div class="dashboard-side">
          <section class="panel">
            <header class="panel-heading"><div><h2>{{ $t('overview.gateway') }}</h2><p>{{ $t('overview.gatewayHint') }}</p></div><span class="inline-status" :class="{ warning: !healthy }">{{ healthy ? $t('common.working') : $t('common.attention') }}</span></header>
            <dl class="health-list">
              <div><dt><span aria-hidden="true"><Icon name="lan" /></span>{{ $t('status.network') }}</dt><dd><strong>{{ health.ethernet.has_ip ? $t('common.connected') : $t('common.disconnected') }}</strong><small>{{ fmt(health.ethernet.ip) }}</small></dd></div>
              <div><dt><span aria-hidden="true"><Icon name="access-point" /></span>{{ $t('status.radio') }}</dt><dd><strong>{{ health.radio.present ? megahertz(health.radio.frequency_hz) : $t('common.unavailable') }}</strong><small>{{ $t('status.networkId') }} {{ health.radio.network_id }}</small></dd></div>
              <div><dt><span aria-hidden="true"><Icon name="clock-outline" /></span>{{ $t('status.time') }}</dt><dd><strong>{{ $t(`status.timeState.${health.time.state}`) }}</strong><small>{{ dateTime(locale, health.time.last_sync_ms) }}</small></dd></div>
              <div><dt><span aria-hidden="true"><Icon name="database" /></span>{{ $t('status.storage') }}</dt><dd><strong>{{ health.storage.ready ? $t('common.ready') : $t('common.attention') }}</strong><small>{{ $t('status.registryGeneration') }} {{ health.registry.generation }}</small></dd></div>
            </dl>
          </section>

          <section class="panel home-assistant-card">
            <header class="panel-heading"><div><h2>Home Assistant</h2><p>{{ $t('ha.cardHint') }}</p></div><span class="inline-status warning">{{ $t('ha.notConfigured') }}</span></header>
            <div class="integration-row"><span class="integration-symbol" aria-hidden="true"><Icon name="home-assistant" /></span><div><strong>{{ $t('ha.connectTitle') }}</strong><p>{{ $t('ha.connectHint') }}</p></div></div>
            <RouterLink class="button primary full" to="/home-assistant">{{ $t('ha.configure') }}</RouterLink>
          </section>
        </div>
      </div>

      <p v-if="updatedAt" class="updated-note">{{ $t('status.updated', { time: timeOfDay(locale, updatedAt) }) }} · {{ $t('status.autoRefresh') }}</p>
      <details class="diagnostics"><summary>{{ $t('common.details') }}</summary><pre>{{ JSON.stringify({ info, health }, null, 2) }}</pre></details>
    </template>
  </div>
</template>
