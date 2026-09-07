import { createRouter, createWebHistory } from 'vue-router'
import { resolveRouteAccess } from './access'
import HomeView from '../views/HomeView.vue'
import SetupView from '../views/SetupView.vue'
import LoginView from '../views/LoginView.vue'
import StatusView from '../views/StatusView.vue'
import NodesView from '../views/NodesView.vue'
import SettingsView from '../views/SettingsView.vue'

// Views are imported eagerly: the production build ships a single application
// bundle (see vite.config.ts), so a lazy route deferred nothing and only cost
// a chunk boundary. Two exceptions earn their chunk -- the pairing QR scanner
// (jsQR), and administration below: users, API tokens and their dialogs grew
// to ~4.8 KB gzip that only an admin who opens the page ever needs.
export const router = createRouter({
  history: createWebHistory(),
  routes: [
    { path: '/', component: HomeView, meta: { chrome: false } },
    { path: '/setup', component: SetupView, meta: { chrome: false } },
    { path: '/login', component: LoginView, meta: { chrome: false } },
    { path: '/status', component: StatusView, meta: { requiresAuth: true } },
    { path: '/nodes', component: NodesView, meta: { requiresAuth: true } },
    { path: '/settings', component: SettingsView, meta: { requiresAuth: true } },
    { path: '/admin', component: () => import('../views/AdminView.vue'), meta: { requiresAuth: true, requiresAdmin: true } },
    { path: '/:pathMatch(.*)*', redirect: '/' },
  ],
})

router.beforeEach(async (to) => {
  return resolveRouteAccess({
    path: to.path,
    fullPath: to.fullPath,
    requiresAuth: to.meta.requiresAuth === true,
    requiresAdmin: to.meta.requiresAdmin === true,
  })
})
