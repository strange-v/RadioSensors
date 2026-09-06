<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue'
import { useRoute } from 'vue-router'
import { useRouter } from 'vue-router'
import { api } from './api/client'
import LanguageSwitcher from './components/LanguageSwitcher.vue'

const route = useRoute()
const router = useRouter()
const uiVersion = __UI_VERSION__
const hostname = ref('RadioSensors')
const reachable = ref(false)
const showChrome = computed(() => route.meta.chrome !== false)

async function loadIdentity() {
  if (!showChrome.value) return
  try {
    const info = await api.info()
    hostname.value = info.hostname
    reachable.value = true
  } catch {
    reachable.value = false
  }
}

async function signOut() {
  try { await api.logout() } catch { /* Expire the local navigation state anyway. */ }
  await router.replace('/login')
}

onMounted(loadIdentity)
watch(showChrome, loadIdentity)
</script>

<template>
  <div v-if="showChrome" class="app-layout">
    <aside class="sidebar">
      <RouterLink class="brand" to="/status" aria-label="RadioSensors gateway">
        <span class="brand-mark" aria-hidden="true"><i></i><i></i><i></i></span>
        <span class="brand-copy"><strong>RadioSensors</strong><small>{{ hostname }}</small></span>
      </RouterLink>

      <nav class="primary-nav" :aria-label="$t('nav.main')">
        <RouterLink to="/status"><span class="nav-icon" aria-hidden="true">⌂</span><span>{{ $t('nav.overview') }}</span></RouterLink>
        <RouterLink to="/nodes"><span class="nav-icon" aria-hidden="true">◉</span><span>{{ $t('nav.nodes') }}</span></RouterLink>
        <RouterLink to="/settings"><span class="nav-icon" aria-hidden="true">⚙</span><span>{{ $t('nav.settings') }}</span></RouterLink>
      </nav>

      <div class="sidebar-status">
        <strong><span class="status-dot" :class="{ offline: !reachable }"></span>{{ reachable ? $t('shell.reachable') : $t('shell.unreachable') }}</strong>
        <small>{{ hostname }}</small>
      </div>
    </aside>

    <div class="workspace">
      <header class="topbar">
        <span class="gateway-address">{{ hostname }}</span>
        <div class="topbar-actions"><LanguageSwitcher /><button class="user-avatar" type="button" :aria-label="$t('shell.signOut')" :title="$t('shell.signOut')" @click="signOut">A</button></div>
      </header>
      <main><RouterView /></main>
      <footer>{{ $t('common.uiVersion', { version: uiVersion }) }}</footer>
    </div>
  </div>

  <div v-else class="standalone-layout">
    <header class="standalone-header">
      <RouterLink class="brand" to="/" aria-label="RadioSensors gateway">
        <span class="brand-mark" aria-hidden="true"><i></i><i></i><i></i></span>
        <span class="brand-copy"><strong>RadioSensors</strong><small>{{ $t('common.gateway') }}</small></span>
      </RouterLink>
      <LanguageSwitcher />
    </header>
    <main><RouterView /></main>
    <footer>{{ $t('common.uiVersion', { version: uiVersion }) }}</footer>
  </div>
</template>
