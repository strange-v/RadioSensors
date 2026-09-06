<script setup lang="ts">
import { ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { api, errorCode } from '../api/client'

const route = useRoute()
const router = useRouter()
const username = ref('')
const password = ref('')
const saving = ref(false)
const failure = ref('')

async function submit() {
  if (!username.value || !password.value || saving.value) return
  saving.value = true
  failure.value = ''
  try {
    await api.login(username.value, password.value)
    const redirect = typeof route.query.redirect === 'string' &&
      route.query.redirect.startsWith('/') ? route.query.redirect : '/status'
    await router.replace(redirect)
  } catch (error) {
    failure.value = errorCode(error)
  } finally {
    saving.value = false
  }
}
</script>

<template>
  <div class="page narrow">
    <div class="page-title"><p class="eyebrow">RadioSensors</p><h1>{{ $t('login.title') }}</h1><p>{{ $t('login.intro') }}</p></div>
    <form class="setup-card" @submit.prevent="submit">
      <label><span>{{ $t('login.username') }}</span><input v-model.trim="username" autocomplete="username" maxlength="32" autofocus></label>
      <label><span>{{ $t('login.password') }}</span><input v-model="password" type="password" autocomplete="current-password" maxlength="128"></label>
      <div v-if="failure" class="notice error">{{ $t(`error.${failure}`) }}</div>
      <button class="button primary full" :disabled="saving || !username || !password" type="submit">{{ saving ? $t('login.signingIn') : $t('login.submit') }}</button>
    </form>
  </div>
</template>
