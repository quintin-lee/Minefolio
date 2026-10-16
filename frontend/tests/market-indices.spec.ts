// frontend/tests/market-indices.spec.ts
import { describe, it, expect, beforeEach, vi } from 'vitest'
import { nextTick } from 'vue'
import { mount } from '@vue/test-utils'
import { createPinia, setActivePinia } from 'pinia'
import ElementPlus from 'element-plus'
import IndexCard from '@/components/market/IndexCard.vue'
import MarketClocksRibbon from '@/components/market/MarketClocksRibbon.vue'
import IndicesFilterBar from '@/components/market/IndicesFilterBar.vue'
import IndexDetailDrawer from '@/components/market/IndexDetailDrawer.vue'
import GlobalIndicesBoard from '@/components/market/GlobalIndicesBoard.vue'
import { useMarketStore } from '@/stores/market'
import type { GlobalIndexItem } from '@/types'

vi.mock('@/api/market', () => ({
  marketApi: {
    getIndices: vi.fn().mockResolvedValue({
      updated_at: '2026-10-16 12:00:00',
      market_clocks: [],
      indices: [],
    }),
  },
}))

describe('Market Components', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
  })

  it('renders IndexCard with positive change and sparkline path', () => {
    const item: GlobalIndexItem = {
      symbol: 'sh000001',
      name: '上证指数',
      category: 'cn',
      region_flag: '🇨🇳',
      current_price: 3285.4,
      change_amount: 25.32,
      change_percent: 0.78,
      open_price: 3262.1,
      high_price: 3292.1,
      low_price: 3258.8,
      prev_close: 3260.08,
      currency: 'CNY',
      quote_time: '2026-10-16 11:30:00',
      sparkline: [3260.1, 3265.4, 3270.8, 3268.2, 3280.1, 3285.4],
    }

    const wrapper = mount(IndexCard, {
      props: { item, direction: 'up' },
    })

    expect(wrapper.text()).toContain('上证指数')
    expect(wrapper.text()).toContain('3,285.40')
    expect(wrapper.text()).toContain('+0.78%')
    expect(wrapper.find('svg polyline').exists() || wrapper.find('svg path').exists()).toBe(true)
    expect(wrapper.classes()).toContain('flash-up')
  })

  it('renders MarketClocksRibbon with exchange clocks', () => {
    const wrapper = mount(MarketClocksRibbon)
    expect(wrapper.find('.clocks-ribbon').exists()).toBe(true)
  })

  it('renders sleep tag in MarketClocksRibbon when all markets are closed', () => {
    const store = useMarketStore()
    store.marketClocks = [
      { market: 'SSE_SZSE', name: 'A股', timezone: 'Asia/Shanghai', status: 'closed', open_time: '09:30', close_time: '15:00' },
      { market: 'HKEX', name: '港股', timezone: 'Asia/Hong_Kong', status: 'closed', open_time: '09:30', close_time: '16:00' },
    ]
    const wrapper = mount(MarketClocksRibbon)
    expect(wrapper.find('.sleep-tag').exists()).toBe(true)
    expect(wrapper.text()).toContain('全部闭市')
  })

  it('renders IndicesFilterBar and emits filter event on search', async () => {
    const wrapper = mount(IndicesFilterBar)
    expect(wrapper.find('.indices-filter-bar').exists()).toBe(true)
    const input = wrapper.findComponent({ name: 'ElInput' })
    if (input.exists()) {
      await input.vm.$emit('input', 'nasdaq')
      expect(wrapper.emitted('update:filter')).toBeTruthy()
      expect(wrapper.emitted('update:filter')![0]).toEqual(['nasdaq'])
    }
  })

  it('renders IndexDetailDrawer when open with index stats', async () => {
    const store = useMarketStore()
    const item: GlobalIndexItem = {
      symbol: 'sh000001',
      name: '上证指数',
      category: 'cn',
      region_flag: '🇨🇳',
      current_price: 3285.4,
      change_amount: 25.32,
      change_percent: 0.78,
      open_price: 3262.1,
      high_price: 3292.1,
      low_price: 3258.8,
      prev_close: 3260.08,
      currency: 'CNY',
      quote_time: '2026-10-16 11:30:00',
      sparkline: [3260.1, 3285.4],
    }
    store.openIndexDrawer(item)

    const wrapper = mount(IndexDetailDrawer, {
      global: {
        stubs: {
          'el-drawer': {
            props: ['modelValue', 'title'],
            template: '<div class="el-drawer" v-if="modelValue"><div class="drawer-title">{{ title }}</div><slot /></div>',
          },
        },
      },
    })
    expect(wrapper.text()).toContain('上证指数')
    expect(wrapper.text()).toContain('3,262.10')
  })

  it('mounts GlobalIndicesBoard and initializes polling lifecycle', () => {
    const store = useMarketStore()
    const fetchSpy = vi.spyOn(store, 'fetchIndices').mockResolvedValue()

    const wrapper = mount(GlobalIndicesBoard)
    expect(wrapper.findComponent(MarketClocksRibbon).exists()).toBe(true)
    expect(wrapper.findComponent(IndexDetailDrawer).exists()).toBe(true)
    expect(fetchSpy).toHaveBeenCalled()

    wrapper.unmount()
  })
})
