<script setup lang="ts">
// Creates one API token. It deliberately does not display the generated
// secret: a dialog can be dismissed by a stray click on the backdrop, and the
// gateway cannot produce that value a second time. The secret is handed to the
// card, which shows it in the page until it is explicitly dismissed.
import { computed, reactive, ref } from 'vue'
import { api, errorCode } from '../api/client'
import type { ApiToken, CreatedApiToken, TokenScope } from '../api/types'
import Modal from './Modal.vue'
import { TOKEN_NAME_MAX_BYTES, TOKEN_SCOPES, findTokenNamed, tokenDraftError } from '../utils/tokens'

const props = defineProps<{ tokens: ApiToken[] }>()
const emit = defineEmits<{ close: []; created: [token: CreatedApiToken] }>()

const draft = reactive<{ name: string; scopes: TokenScope[] }>({ name: '', scopes: [...TOKEN_SCOPES] })
const busy = ref(false)
const failure = ref('')

const problem = computed(() => tokenDraftError(draft, props.tokens))
// The gateway accepts duplicate names, so this is a warning, not a block --
// but two identical rows in the list are impossible to tell apart later.
const duplicate = computed(() => draft.name !== '' && findTokenNamed(props.tokens, draft.name) !== undefined)

async function create() {
  if (problem.value) return
  busy.value = true
  failure.value = ''
  try {
    emit('created', await api.createToken(draft.name, draft.scopes))
  } catch (error) {
    failure.value = errorCode(error)
  } finally {
    busy.value = false
  }
}
</script>

<template>
  <Modal :title="$t('tokens.addTitle')" @close="emit('close')">
    <div class="form-stack">
      <label>
        <span>{{ $t('tokens.name') }}</span>
        <input v-model.trim="draft.name" :maxlength="TOKEN_NAME_MAX_BYTES" autocapitalize="none" autocomplete="off" spellcheck="false">
        <small>{{ $t('tokens.nameHint') }}</small>
      </label>

      <fieldset class="scope-set">
        <legend>{{ $t('tokens.scopes') }}</legend>
        <label v-for="scope in TOKEN_SCOPES" :key="scope" class="scope-row">
          <input v-model="draft.scopes" type="checkbox" :value="scope">
          <span>{{ $t(`tokens.scope.${scope.replace(':', '_')}`) }}</span>
        </label>
      </fieldset>

      <div v-if="problem" class="notice">{{ $t(`tokens.problem.${problem}`) }}</div>
      <div v-else-if="duplicate" class="notice">{{ $t('tokens.duplicateName') }}</div>
      <div v-if="failure" class="notice error">{{ $t(`error.${failure}`) }}</div>

      <div class="modal-actions">
        <button class="button secondary" type="button" @click="emit('close')">{{ $t('common.cancel') }}</button>
        <button class="button primary" :disabled="busy || problem !== ''" type="button" @click="create">
          {{ busy ? $t('tokens.creating') : $t('tokens.create') }}
        </button>
      </div>
    </div>
  </Modal>
</template>

<style scoped>
.scope-set { display: grid; gap: var(--space-2); margin: 0; padding: 0; border: 0; }
.scope-set legend { padding: 0; color: var(--ink-soft); font-size: var(--text-sm); font-weight: 500; }
.scope-row { display: flex; align-items: center; gap: var(--space-2); color: var(--ink-soft); font-size: var(--text-sm); }
.scope-row input { margin: 0; }
</style>
