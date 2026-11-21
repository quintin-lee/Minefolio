import { describe, it, expect, vi, beforeEach } from 'vitest'
import http from '@/utils/http'
import { marketApi } from '@/api/market'

vi.mock('@/utils/http', () => ({
  default: {
    get: vi.fn(),
    post: vi.fn(),
    put: vi.fn(),
  },
}))

describe('marketApi async task handling', () => {
  beforeEach(() => {
    vi.clearAllMocks()
  })

  it('resolves syncAll via async task polling', async () => {
    vi.mocked(http.post).mockResolvedValueOnce({
      task_id: 21,
      task_type: 'market_sync_all',
      status: 'pending',
      deduplicated: false,
    })
    vi.mocked(http.get).mockResolvedValueOnce({
      id: 21,
      status: 'completed',
      progress: 100,
      message: '已完成',
      result: { synced_count: 3, failed_count: 1 },
    })

    const res = await marketApi.syncAll()
    expect(http.post).toHaveBeenCalledWith('/market/sync')
    expect(http.get).toHaveBeenCalledWith('/tasks/21')
    expect(res).toEqual({ synced_count: 3, failed_count: 1 })
  })

  it('resolves syncSingle via async task polling and keeps direct payload compatibility', async () => {
    vi.mocked(http.post).mockResolvedValueOnce({
      task_id: 22,
      task_type: 'market_sync_single',
      status: 'pending',
      deduplicated: true,
    })
    vi.mocked(http.get).mockResolvedValueOnce({
      id: 22,
      status: 'completed',
      progress: 100,
      message: '已完成',
      result: { symbol: 'AAPL', name: 'Apple', current_price: 210, net_value: 210 },
    })

    const asyncRes = await marketApi.syncSingle(7)
    expect(http.post).toHaveBeenCalledWith('/market/sync/7')
    expect(asyncRes.symbol).toBe('AAPL')

    vi.mocked(http.post).mockResolvedValueOnce({
      symbol: 'MSFT',
      name: 'Microsoft',
      current_price: 420,
    })
    const syncRes = await marketApi.syncSingle(8)
    expect(syncRes.symbol).toBe('MSFT')
  })

  it('resolves syncExchangeRates via async task polling', async () => {
    vi.mocked(http.post).mockResolvedValueOnce({
      task_id: 23,
      task_type: 'market_fx_sync',
      status: 'pending',
      deduplicated: false,
    })
    vi.mocked(http.get).mockResolvedValueOnce({
      id: 23,
      status: 'completed',
      progress: 100,
      message: '已完成',
      result: { USD: 7.24, EUR: 7.86 },
    })

    const rates = await marketApi.syncExchangeRates()
    expect(http.post).toHaveBeenCalledWith('/market/exchange-rates/sync')
    expect(http.get).toHaveBeenCalledWith('/tasks/23')
    expect(rates.USD).toBe(7.24)
  })
})
