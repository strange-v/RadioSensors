import { createI18n } from 'vue-i18n'
import { en } from './en'
import { preferredLocale } from './locales'
import { uk } from './uk'

const locale = preferredLocale()
document.documentElement.lang = locale

export const i18n = createI18n({
  legacy: false,
  locale,
  fallbackLocale: 'en',
  messages: { en, uk },
})
