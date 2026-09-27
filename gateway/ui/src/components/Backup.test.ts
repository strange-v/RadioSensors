// @vitest-environment jsdom
import { flushPromises, mount } from '@vue/test-utils'
import { createI18n } from 'vue-i18n'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { en } from '../i18n/en'

const mocked = vi.hoisted(() => ({
  exportBackup: vi.fn(), previewBackup: vi.fn(), restoreBackup: vi.fn(), download: vi.fn(), encode: vi.fn(), setupStatus: vi.fn(),
}))
vi.mock('../api/client', () => ({ api: mocked, errorCode: (e: Error) => e.message }))
vi.mock('../utils/backup', async original => ({ ...await original<typeof import('../utils/backup')>(), downloadBackup: mocked.download, encodeBackupFile: mocked.encode }))
import BackupCard from './BackupCard.vue'
import RestoreBackup from './RestoreBackup.vue'
import SetupView from '../views/SetupView.vue'

const global = { plugins: [createI18n({ legacy: false, locale: 'en', messages: { en } })], stubs: { RouterLink: true } }
const open = { setup_required: true, physical_window_active: true, remaining_seconds: 600 }
beforeEach(() => {
  vi.resetAllMocks()
  mocked.exportBackup.mockResolvedValue(new Blob(['encrypted']))
  mocked.encode.mockResolvedValue('encrypted-base64')
  mocked.previewBackup.mockResolvedValue({ gateway_id: '1234', node_count: 5, created_at_ms: 0, hostname: 'restored', mdns_enabled: true })
  mocked.restoreBackup.mockResolvedValue(undefined)
  mocked.setupStatus.mockResolvedValue(open)
})
afterEach(() => vi.useRealTimers())
async function select(wrapper: ReturnType<typeof mount>) {
  const input = wrapper.get('input[type=file]')
  Object.defineProperty(input.element, 'files', { configurable: true, value: [new File(['backup'], 'backup.oskbackup')] })
  await input.trigger('change'); await flushPromises()
  await wrapper.get('input[type=password]').setValue('backup password')
}
describe('backup UI', () => {
  it('requires matching passwords and downloads only after successful export', async () => {
    const w = mount(BackupCard, { global })
    await w.findAll('input')[0].setValue('backup password')
    await w.findAll('input')[1].setValue('different password')
    await w.get('form').trigger('submit'); expect(mocked.exportBackup).not.toHaveBeenCalled()
    await w.findAll('input')[1].setValue('backup password')
    await w.get('form').trigger('submit'); await flushPromises()
    expect(mocked.download).toHaveBeenCalledOnce()
    expect((w.findAll('input')[0].element as HTMLInputElement).value).toBe('')
    w.unmount()
  })
  it('requires physical presence and never writes during preview', async () => {
    const w = mount(RestoreBackup, { global, props: { status: { ...open, physical_window_active: false } } })
    await select(w); await w.get('form').trigger('submit'); expect(mocked.previewBackup).not.toHaveBeenCalled()
    await w.setProps({ status: open }); await w.get('form').trigger('submit'); await flushPromises()
    expect(w.text()).toContain('1234'); expect(mocked.restoreBackup).not.toHaveBeenCalled()
    await w.get('input[type=password]').setValue('other password')
    expect(w.find('dl').exists()).toBe(false)
    w.unmount()
  })
  it('shows a damaged-file error and never exposes a restore action', async () => {
    mocked.previewBackup.mockRejectedValue(new Error('invalid_backup'))
    const w = mount(RestoreBackup, { global, props: { status: open } })
    await select(w); await w.get('form').trigger('submit'); await flushPromises()
    expect(w.get('[role=alert]').text()).toContain('Wrong backup password')
    expect(w.find('input[type=checkbox]').exists()).toBe(false)
    expect(mocked.restoreBackup).not.toHaveBeenCalled()
    w.unmount()
  })
  it('requires a new admin and explicit confirmation, then keeps the restart result', async () => {
    const w = mount(RestoreBackup, { global, props: { status: open } })
    await select(w); await w.get('form').trigger('submit'); await flushPromises()
    await w.findAll('form')[1].trigger('submit'); expect(mocked.restoreBackup).not.toHaveBeenCalled()
    await w.get('input[autocomplete=username]').setValue('admin')
    await w.get('input[autocomplete=new-password]').setValue('admin password')
    await w.get('input[type=checkbox]').setValue(true)
    await w.findAll('form')[1].trigger('submit'); await flushPromises()
    expect(mocked.restoreBackup).toHaveBeenCalledWith('encrypted-base64', 'backup password', 'admin', 'admin password')
    expect(w.text()).toContain('Backup restored')
    expect(w.get('a').attributes('href')).toBe('http://restored.local/login')
    expect(w.emitted('complete')).toHaveLength(1)
    w.unmount()
  })
  it('shows reset instructions instead of setup and restore controls after interrupted recovery', async () => {
    mocked.setupStatus.mockResolvedValue({ ...open, recovery_required: true, physical_window_active: false })
    vi.useFakeTimers()
    const w = mount(SetupView, { global }); await flushPromises()
    expect(w.text()).toContain('Gateway reset required')
    expect(w.find('form').exists()).toBe(false)
    expect(w.find('.setup-mode').exists()).toBe(false)
    w.unmount()
  })
})
