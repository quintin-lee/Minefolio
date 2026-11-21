import { describe, it, expect, vi, beforeEach } from 'vitest'
import { defineComponent, ref } from 'vue'
import { mount } from '@vue/test-utils'
import { useChatEntityPopover } from '@/composables/chat/useChatEntityPopover'
import { useChatAttachments, formatAttachmentSize } from '@/composables/chat/useChatAttachments'
import { useChatAnalysisPanel } from '@/composables/chat/useChatAnalysisPanel'
import type { AiMessage } from '@/api/ai'

const mockGetEntityDetail = vi.fn()
const mockGetCollisionEntities = vi.fn()
const mockLoadEntities = vi.fn()

vi.mock('@/composables/useFinancialEntities', () => ({
  useFinancialEntities: () => ({
    loadEntities: mockLoadEntities,
    getEntityDetail: mockGetEntityDetail,
    getCollisionEntities: mockGetCollisionEntities,
  }),
}))

function mountComposable<T>(factory: () => T) {
  let result!: T
  const Comp = defineComponent({
    setup() {
      result = factory()
      return {}
    },
    template: '<div />',
  })
  const wrapper = mount(Comp)
  return { result, wrapper }
}

describe('useChatEntityPopover', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    vi.useFakeTimers()
  })

  it('toggles visibility on click and navigates on handlePopoverNavigate', () => {
    const onNavigate = vi.fn()
    mockGetEntityDetail.mockReturnValue({ id: '42', type: 'asset', name: '沪深300' })
    mockGetCollisionEntities.mockReturnValue(null)

    const { result, wrapper } = mountComposable(() => useChatEntityPopover({ onNavigate }))
    const rect = new DOMRect(10, 20, 100, 30)
    const payload = { el: document.createElement('span'), type: 'asset' as const, id: '42', name: '沪深300', rect }

    result.handleEntityClick(payload)
    expect(result.popoverVisible.value).toBe(true)
    expect(result.popoverDetail.value?.id).toBe('42')

    // Same target toggles closed
    result.handleEntityClick(payload)
    expect(result.popoverVisible.value).toBe(false)

    // Reopen and navigate closes popover and calls callback
    result.handleEntityClick(payload)
    result.handlePopoverNavigate({ type: 'asset', id: '42' })
    expect(result.popoverVisible.value).toBe(false)
    expect(onNavigate).toHaveBeenCalledWith({ type: 'asset', id: '42' })

    wrapper.unmount()
  })

  it('shows popover on hover after 180ms delay and hides on leave after 220ms', () => {
    mockGetEntityDetail.mockReturnValue({ id: '7', type: 'account', name: '招商银行' })
    mockGetCollisionEntities.mockReturnValue(null)

    const { result, wrapper } = mountComposable(() => useChatEntityPopover({ onNavigate: vi.fn() }))
    const payload = {
      el: document.createElement('span'),
      type: 'account' as const,
      id: '7',
      name: '招商银行',
      rect: new DOMRect(0, 0, 50, 20),
    }

    result.handleEntityHover(payload)
    expect(result.popoverVisible.value).toBe(false)
    vi.advanceTimersByTime(180)
    expect(result.popoverVisible.value).toBe(true)

    result.handleEntityLeave()
    vi.advanceTimersByTime(220)
    expect(result.popoverVisible.value).toBe(false)

    wrapper.unmount()
  })
})

describe('useChatAttachments', () => {
  it('formats byte sizes consistently', () => {
    expect(formatAttachmentSize(512)).toBe('512 B')
    expect(formatAttachmentSize(2048)).toBe('2.0 KB')
    expect(formatAttachmentSize(3 * 1024 * 1024)).toBe('3.0 MB')
  })

  it('builds structured OCR prompt on successful receipt scan and falls back on error', async () => {
    const scanReceipt = vi.fn().mockResolvedValue({
      amount: 88.5,
      counterparty: '超市',
      date: '2026-11-21',
      currency: 'CNY',
      category_name: '餐饮',
      description: '午餐',
      type: 'expense',
    })
    const { result } = mountComposable(() => useChatAttachments({ scanReceipt }))

    const dummyImg = {
      file: new File(['x'], 'receipt.png', { type: 'image/png' }),
      dataUrl: 'data:image/png;base64,abc',
      name: 'receipt.png',
      sizeText: '1 B',
    }
    const prompt = await result.scanReceiptAttachment(dummyImg, '帮我入账')
    expect(prompt).toContain('¥88.5')
    expect(prompt).toContain('超市')
    expect(prompt).toContain('帮我入账')
    expect(result.isScanningReceipt.value).toBe(false)

    scanReceipt.mockRejectedValueOnce(new Error('network error'))
    const fallbackPrompt = await result.scanReceiptAttachment(dummyImg, '备注')
    expect(fallbackPrompt).toContain('[已上传消费票据截图: receipt.png]')
    expect(fallbackPrompt).toContain('备注')
    expect(result.isScanningReceipt.value).toBe(false)
  })
})

describe('useChatAnalysisPanel', () => {
  beforeEach(() => {
    localStorage.clear()
  })

  it('extracts panel items, enforces MAX_PANEL_ITEMS cap=20, and handles tab removal', () => {
    const isMobile = ref(false)
    const messages = ref<AiMessage[]>([
      {
        id: 101,
        role: 'assistant',
        content: '```mermaid\npie title 资产\n "A": 50\n```\n',
        created_at: '2026-11-21',
      } as AiMessage,
    ])

    const { result, wrapper } = mountComposable(() =>
      useChatAnalysisPanel({ messages: () => messages.value, isMobile })
    )

    result.syncLatestAssistantMessage(messages.value[0])
    expect(result.panelItems.value.length).toBe(1)
    expect(result.panelVisible.value).toBe(true)
    const firstId = result.panelItems.value[0]!.id
    expect(result.activeTabId.value).toBe(firstId)
    expect(result.extractedSegmentIdMap.value[101]).toBeDefined()

    // Mobile mode disables extractedSegmentIdMap so cards render inline
    isMobile.value = true
    expect(result.extractedSegmentIdMap.value).toEqual({})

    result.handleRemovePanelTab(firstId)
    expect(result.panelItems.value.length).toBe(0)
    expect(result.panelVisible.value).toBe(false)
    expect(result.activeTabId.value).toBe('')

    wrapper.unmount()
  })

  it('loads clamped panel width from localStorage and resets panel state', () => {
    localStorage.setItem('minefolio:analysis-panel-width', '520')
    const isMobile = ref(false)
    const { result, wrapper } = mountComposable(() =>
      useChatAnalysisPanel({ messages: () => [], isMobile })
    )

    result.loadPanelWidth()
    expect(result.panelWidth.value).toBe(520)

    localStorage.setItem('minefolio:analysis-panel-width', '9999')
    result.loadPanelWidth()
    expect(result.panelWidth.value).toBe(520) // rejects out-of-range

    result.panelVisible.value = true
    result.activeTabId.value = 'tab-1'
    result.resetPanel()
    expect(result.panelVisible.value).toBe(false)
    expect(result.activeTabId.value).toBe('')
    expect(result.panelItems.value).toEqual([])

    wrapper.unmount()
  })
})
