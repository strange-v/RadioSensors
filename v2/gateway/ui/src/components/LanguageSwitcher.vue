<script setup lang="ts">
import { useI18n } from 'vue-i18n'
const { locale } = useI18n()
function choose(value: 'en' | 'uk') { locale.value = value; localStorage.setItem('radiosensors.locale', value); document.documentElement.lang = value }
</script>
<template><div class="language-switcher" role="group" aria-label="Language"><button :class="{ active: locale === 'en' }" type="button" @click="choose('en')">EN</button><button :class="{ active: locale === 'uk' }" type="button" @click="choose('uk')">UA</button></div></template>
