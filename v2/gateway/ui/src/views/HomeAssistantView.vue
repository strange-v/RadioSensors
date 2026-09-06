<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { api, errorCode } from '../api/client'
import Icon from '../components/Icon.vue'
import type { GatewayInfo } from '../api/types'

const info = ref<GatewayInfo | null>(null)
const token = ref('')
const creating = ref(false)
const copied = ref(false)
const failure = ref('')

async function createToken() {
  creating.value = true
  failure.value = ''
  try {
    const created = await api.createToken('Home Assistant', ['gateway:read', 'registry:read', 'telemetry:read'])
    token.value = created.token
  } catch (error) { failure.value = errorCode(error) }
  finally { creating.value = false }
}

async function copyToken() {
  await navigator.clipboard.writeText(token.value)
  copied.value = true
}
onMounted(async () => { try { info.value = await api.info() } catch { /* The page remains useful with fallback text. */ } })
</script>

<template>
  <div class="page narrow-workflow">
    <div class="workflow-heading"><span class="workflow-symbol" aria-hidden="true"><Icon name="home-assistant" /></span><p class="eyebrow">Home Assistant</p><h1>{{ $t('ha.title') }}</h1><p>{{ $t('ha.subtitle') }}</p></div>

    <div class="workflow-steps">
      <section class="panel workflow-step"><span class="step-number">1</span><div><h2>{{ $t('ha.stepTokenTitle') }}</h2><p>{{ $t('ha.stepTokenHint') }}</p><div v-if="failure" class="notice error">{{ $t(`error.${failure}`) }}</div><template v-if="token"><code class="generated-token">{{ token }}</code><button class="button secondary" type="button" @click="copyToken">{{ copied ? $t('ha.copied') : $t('ha.copyToken') }}</button><small class="availability-note">{{ $t('ha.saveToken') }}</small></template><button v-else class="button primary" :disabled="creating" type="button" @click="createToken">{{ creating ? $t('ha.creatingToken') : $t('ha.createToken') }}</button></div></section>
      <section class="panel workflow-step"><span class="step-number">2</span><div><h2>{{ $t('ha.stepAddTitle') }}</h2><p>{{ $t('ha.stepAddHint') }}</p><dl class="connection-details"><div><dt>{{ $t('ha.address') }}</dt><dd>{{ info?.hostname || 'rf-gateway.local' }}</dd></div><div><dt>{{ $t('ha.integration') }}</dt><dd>RadioSensors</dd></div></dl></div></section>
    </div>

    <div class="workflow-actions"><RouterLink class="button secondary" to="/status">{{ $t('ha.later') }}</RouterLink></div>
  </div>
</template>
