// @vitest-environment jsdom
// The card must never pass the gateway's wanted level off as the level the
// node uses, and must not let an administrator ask for more than the node's
// hardware allows.
import { flushPromises, mount } from '@vue/test-utils'
import { createI18n } from 'vue-i18n'
import type { Ref } from 'vue'
import { beforeEach, describe, expect, it, vi } from 'vitest'
import { en } from '../i18n/en'
import type { GatewayNode } from '../api/types'

const state = vi.hoisted(() => ({ admin: null as unknown as Ref<boolean> }))
const gatewayApi = vi.hoisted(() => ({ setPowerPolicy: vi.fn(async () => ({ node_id: 7, registry_generation: 2 })) }))
vi.mock('../api/client', async () => {
  const { ref } = await import('vue')
  state.admin = ref(true)
  return { api: gatewayApi, errorCode: () => 'generic', isAdmin: state.admin }
})

import NodeRadio from './NodeRadio.vue'

const node = (over: Partial<GatewayNode> = {}): GatewayNode => ({
  node_id: 7, device_uid: '102132435465768798A9', display_name: 'Hall', profile_id: 5, firmware: '0.1.0',
  state: 'active', has_telemetry: true, rssi: -70, max_power_level: 5, power_policy: 'auto',
  tx_power_target: 3, tx_power_level: 3, radio_fallback: false, supply_limited: false, downlink_rssi: -72, ...over,
})

const mountCard = async (target = node()) => {
  const wrapper = mount(NodeRadio, {
    props: { node: target },
    global: { plugins: [createI18n({ legacy: false, locale: 'en', messages: { en } })] },
  })
  await flushPromises()
  return wrapper
}

beforeEach(() => {
  state.admin.value = true
  vi.clearAllMocks()
})

describe('NodeRadio', () => {
  it('shows the reported level against the ceiling and both directions of the link', async () => {
    const wrapper = await mountCard()
    const details = wrapper.get('.simple-details').text()
    expect(details).toContain('3 of 5')
    expect(details).toContain('-70 dBm')
    expect(details).toContain('-72 dBm')
    expect(wrapper.find('.radio-target').exists()).toBe(false)
  })

  it('shows a wanted level apart from the one the node still uses', async () => {
    const wrapper = await mountCard(node({ tx_power_target: 5 }))
    expect(wrapper.get('.simple-details').text()).toContain('3 of 5')
    expect(wrapper.get('.radio-target').text()).toContain('5')
  })

  it('says when the node has fallen back or is limited by its supply', async () => {
    const wrapper = await mountCard(node({ radio_fallback: true, supply_limited: true }))
    expect(wrapper.get('.radio-fallback').text()).toBe(en.radio.fallback)
    expect(wrapper.get('.radio-supply').text()).toBe(en.radio.supplyLimited)
  })

  it('keeps a fixed level within the ceiling', async () => {
    const wrapper = await mountCard()
    await wrapper.findAll('.segmented button')[1].trigger('click')
    await wrapper.get('input').setValue('6')
    expect(wrapper.get('.button.primary').attributes('disabled')).toBeDefined()
    expect(wrapper.find('small.invalid').exists()).toBe(true)

    await wrapper.get('input').setValue('4')
    await wrapper.get('.button.primary').trigger('click')
    await flushPromises()
    expect(gatewayApi.setPowerPolicy).toHaveBeenCalledWith(7, { power_policy: 'fixed', fixed_power_level: 4 })
    expect(wrapper.emitted('updated')).toHaveLength(1)
  })

  it('returns a fixed node to automatic control', async () => {
    const wrapper = await mountCard(node({ power_policy: 'fixed', fixed_power_level: 2 }))
    expect(wrapper.get('.button.primary').attributes('disabled')).toBeDefined()
    await wrapper.findAll('.segmented button')[0].trigger('click')
    await wrapper.get('.button.primary').trigger('click')
    await flushPromises()
    expect(gatewayApi.setPowerPolicy).toHaveBeenCalledWith(7, { power_policy: 'auto' })
  })

  it('says when no report has arrived since the gateway started', async () => {
    const wrapper = await mountCard(node({ has_telemetry: false, tx_power_level: undefined, rssi: undefined, downlink_rssi: undefined }))
    expect(wrapper.get('.simple-details').text()).toContain(en.radio.noReport)
  })

  it('shows a viewer the policy without controls', async () => {
    state.admin.value = false
    const wrapper = await mountCard(node({ power_policy: 'fixed', fixed_power_level: 2 }))
    expect(wrapper.find('.segmented').exists()).toBe(false)
    expect(wrapper.get('.radio-hint').text()).toBe('Fixed at level 2')
  })
})
