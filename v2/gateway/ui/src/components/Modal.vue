<script setup lang="ts">
// Reusable dialog chrome: backdrop + panel + a fixed-height header (title and
// close button only, no subtitle). Body content is left entirely to the
// caller via the default slot. Use this for every new popup instead of
// hand-rolling backdrop/panel markup.
import Icon from './Icon.vue'

defineProps<{ title: string }>()
const emit = defineEmits<{ close: [] }>()
</script>

<template>
  <div class="modal-backdrop" @click.self="emit('close')">
    <section class="modal" role="dialog" aria-modal="true" :aria-label="title">
      <header class="modal-header">
        <h2>{{ title }}</h2>
        <button class="icon-button" type="button" :aria-label="$t('common.close')" @click="emit('close')">
          <Icon name="close" />
        </button>
      </header>
      <div class="modal-body">
        <slot />
      </div>
    </section>
  </div>
</template>

<style scoped>
.modal-backdrop { position: fixed; inset: 0; z-index: 10; display: grid; place-items: center; padding: var(--space-4); background: #000000a6; }
.modal { width: min(500px, 100%); max-height: calc(100vh - 32px); overflow: auto; border: 1px solid var(--line); border-radius: var(--radius-lg); background: var(--surface); box-shadow: var(--shadow-lg); }
.modal-header { display: flex; align-items: center; justify-content: space-between; gap: var(--space-4); height: 52px; flex: none; padding: 0 var(--space-5); border-bottom: 1px solid var(--line); }
.modal-header h2 { font-size: var(--text-lg); }
.modal-body { padding: var(--space-5); }

@media (max-width: 640px) {
  .modal-body { padding: var(--space-4); }
}
</style>
