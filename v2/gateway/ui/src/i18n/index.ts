import { createI18n } from 'vue-i18n'
import { en } from './en'
import { uk } from './uk'

const saved = localStorage.getItem('radiosensors.locale')
const browserLocale = navigator.language.toLowerCase().startsWith('uk') ? 'uk' : 'en'
const locale = saved === 'uk' || saved === 'en' ? saved : browserLocale
document.documentElement.lang = locale

export const i18n = createI18n({
  legacy: false,
  locale,
  fallbackLocale: 'en',
  messages: { en, uk },
})
