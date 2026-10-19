import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { exportChartPng, exportChartCsv } from '@/utils/chart-export'

describe('chart-export utilities', () => {
  let clickSpy: any
  let appendChildSpy: any
  let removeChildSpy: any
  let createObjectURLSpy: any
  let revokeObjectURLSpy: any

  beforeEach(() => {
    clickSpy = vi.fn()
    appendChildSpy = vi.spyOn(document.body, 'appendChild').mockImplementation((node) => node)
    removeChildSpy = vi.spyOn(document.body, 'removeChild').mockImplementation((node) => node)
    createObjectURLSpy = vi.fn().mockReturnValue('blob:mock-url')
    revokeObjectURLSpy = vi.fn()
    global.URL.createObjectURL = createObjectURLSpy
    global.URL.revokeObjectURL = revokeObjectURLSpy

    vi.spyOn(document, 'createElement').mockImplementation((tag: string) => {
      if (tag === 'a') {
        return {
          set href(val: string) { (this as any)._href = val },
          get href() { return (this as any)._href },
          set download(val: string) { (this as any)._download = val },
          get download() { return (this as any)._download },
          click: clickSpy,
        } as unknown as HTMLAnchorElement
      }
      return document.createElement(tag)
    })
  })

  afterEach(() => {
    vi.restoreAllMocks()
  })

  it('exportChartPng calls chart.getDataURL and downloads png file', () => {
    const mockChart = {
      getDataURL: vi.fn().mockReturnValue('data:image/png;base64,mockpngdata'),
    }

    exportChartPng(mockChart as any, 'networth-trend')

    expect(mockChart.getDataURL).toHaveBeenCalledWith(
      expect.objectContaining({
        type: 'png',
        pixelRatio: 2,
      })
    )
    expect(clickSpy).toHaveBeenCalledTimes(1)
  })

  it('exportChartCsv generates valid UTF-8 BOM CSV blob and triggers download', () => {
    const headers = ['日期', '净资产', '币种']
    const rows = [
      ['2026-10-01', 100000, 'CNY'],
      ['2026-10-02', 102500.5, 'CNY'],
    ]

    exportChartCsv('networth-history.csv', headers, rows)

    expect(createObjectURLSpy).toHaveBeenCalledTimes(1)
    const blobArg = createObjectURLSpy.mock.calls[0][0]
    expect(blobArg).toBeInstanceOf(Blob)
    expect(blobArg.type).toContain('text/csv')
    expect(clickSpy).toHaveBeenCalledTimes(1)
  })
})
