import { createRouter, createWebHistory } from 'vue-router'
import { api } from '../api/client'

export const router = createRouter({
  history: createWebHistory(),
  routes: [
    { path: '/', component: () => import('../views/HomeView.vue'), meta: { chrome: false } },
    { path: '/setup', component: () => import('../views/SetupView.vue'), meta: { chrome: false } },
    { path: '/login', component: () => import('../views/LoginView.vue'), meta: { chrome: false } },
    { path: '/status', component: () => import('../views/StatusView.vue'), meta: { requiresAuth: true } },
    { path: '/nodes', component: () => import('../views/NodesView.vue'), meta: { requiresAuth: true } },
    { path: '/home-assistant', component: () => import('../views/HomeAssistantView.vue'), meta: { requiresAuth: true } },
    { path: '/settings', component: () => import('../views/SettingsView.vue'), meta: { requiresAuth: true } },
    { path: '/:pathMatch(.*)*', redirect: '/' },
  ],
})

router.beforeEach(async (to) => {
  if (!to.meta.requiresAuth) return true
  try {
    await api.session()
    return true
  } catch {
    return { path: '/login', query: { redirect: to.fullPath } }
  }
})
