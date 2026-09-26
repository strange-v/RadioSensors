<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, reactive, ref } from 'vue'
import { useI18n } from 'vue-i18n'
import { api, errorCode } from '../api/client'
import type { SetupStatus } from '../api/types'
import Icon from '../components/Icon.vue'
import { HOSTNAME_MAX_BYTES, isValidHostname } from '../utils/format'
const { t } = useI18n(), status = ref<SetupStatus | null>(null), loadError = ref(''), submitError = ref(''), saving = ref(false), completed = ref(false), attempted = ref(false)
const form = reactive({ username: '', password: '', hostname: '', networkId: '' })
let timer: number | undefined
const byteLength = (value: string) => new TextEncoder().encode(value).length
const errors = computed(() => ({
  username: !form.username ? t('validation.required') : !/^[a-z0-9._-]{1,32}$/.test(form.username) ? t('validation.username') : '',
  password: !form.password ? t('validation.required') : byteLength(form.password) < 8 || byteLength(form.password) > 128 ? t('validation.password') : '',
  hostname: isValidHostname(form.hostname) ? '' : t('validation.hostname'),
  networkId: form.networkId && (!/^\d+$/.test(form.networkId) || +form.networkId < 1 || +form.networkId > 255) ? t('validation.networkId') : '',
}))
const invalid = computed(() => Object.values(errors.value).some(Boolean))
async function refresh() { try { status.value = await api.setupStatus(); loadError.value = '' } catch (error) { loadError.value = errorCode(error) } }
async function submit() {
  attempted.value = true; submitError.value = ''
  if (invalid.value || !status.value?.physical_window_active) return
  saving.value = true
  try {
    await api.setup({ username: form.username, password: form.password, ...(form.hostname ? { hostname: form.hostname } : {}), ...(form.networkId ? { operational_network_id: Number(form.networkId) } : {}) })
    completed.value = true
  } catch (error) { submitError.value = errorCode(error); await refresh() } finally { saving.value = false }
}
onMounted(() => { refresh(); timer = window.setInterval(refresh, 1000) })
onBeforeUnmount(() => window.clearInterval(timer))
</script>
<template><div class="page narrow">
  <div v-if="completed" class="success-panel"><span class="state-icon success"><Icon name="check" /></span><h1>{{ $t('setup.successTitle') }}</h1><p>{{ $t('setup.success') }}</p><div class="success-actions"><RouterLink class="button primary" to="/status">{{ $t('setup.continue') }}</RouterLink></div></div>
  <template v-else><div v-if="!status || status.setup_required" class="page-title"><p class="eyebrow">{{ $t('setup.eyebrow') }}</p><h1>{{ $t('setup.title') }}</h1><p>{{ $t('setup.intro') }}</p></div>
    <div v-if="loadError" class="notice error"><strong>{{ $t('error.title') }}</strong><span>{{ $t(`error.${loadError}`) }}</span></div>
    <section v-else-if="status && !status.setup_required" class="empty-state panel"><span class="state-icon info" aria-hidden="true"><Icon name="check" /></span><h1>{{ $t('setup.alreadyConfigured') }}</h1><p>{{ $t('setup.alreadyConfiguredHint') }}</p><RouterLink class="button primary" to="/status">{{ $t('setup.continue') }}</RouterLink></section>
    <form v-else class="setup-card" novalidate @submit.prevent="submit">
      <div class="physical-status" :class="{ open: status?.physical_window_active }"><span class="pulse" aria-hidden="true"></span><div><strong>{{ $t('setup.physicalTitle') }}</strong><p>{{ status?.physical_window_active ? $t('setup.physicalOpen', { seconds: status.remaining_seconds }) : $t('setup.physicalClosed') }}</p></div></div>
      <label><span>{{ $t('setup.username') }}</span><input v-model.trim="form.username" autocomplete="username" maxlength="32" placeholder="admin"><small :class="{ invalid: attempted && errors.username }">{{ attempted && errors.username ? errors.username : $t('setup.usernameHint') }}</small></label>
      <label><span>{{ $t('setup.password') }}</span><input v-model="form.password" type="password" autocomplete="new-password" maxlength="128"><small :class="{ invalid: attempted && errors.password }">{{ attempted && errors.password ? errors.password : $t('setup.passwordHint') }}</small></label>
      <label><span>{{ $t('setup.hostname') }}</span><input v-model.trim="form.hostname" autocapitalize="none" autocomplete="off" spellcheck="false" :maxlength="HOSTNAME_MAX_BYTES" :placeholder="$t('setup.hostnamePlaceholder')"><small v-if="attempted && errors.hostname" class="invalid">{{ errors.hostname }}</small><small v-else class="availability-note">{{ $t('setup.hostnameHint') }}</small></label>
      <details><summary>{{ $t('setup.advanced') }}</summary><label><span>{{ $t('setup.networkId') }}</span><input v-model.trim="form.networkId" inputmode="numeric" placeholder="Auto"><small :class="{ invalid: attempted && errors.networkId }">{{ attempted && errors.networkId ? errors.networkId : $t('setup.networkHint') }}</small></label></details>
      <div v-if="submitError" class="notice error">{{ $t(`error.${submitError}`) }}</div><button class="button primary full" :disabled="saving || !status?.physical_window_active" type="submit">{{ saving ? $t('setup.saving') : $t('setup.submit') }}</button>
    </form>
  </template>
</div></template>
