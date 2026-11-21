import { describe, it, expect, vi, beforeEach } from 'vitest'
import http from '@/utils/http'
import { pollTask, resolveMaybeAsync, isAsyncTaskSubmission } from '@/api/tasks'
import { receiptsApi } from '@/api/receipts'
import { transactionsApi } from '@/api/transactions'
import { dailyExpensesApi } from '@/api/daily_expenses'
import { backupApi } from '@/api/backup'
import { assetsApi } from '@/api/assets'
import { testMcpServer, refreshMcpServer } from '@/api/ai-mcp'
import { testSkill } from '@/api/skills'

vi.mock('@/utils/http', () => ({
  default: {
    get: vi.fn(),
    post: vi.fn(),
  },
}))

describe('tasks API & pollTask', () => {
  beforeEach(() => {
    vi.clearAllMocks()
  })

  it('recognizes async task submission envelopes', () => {
    expect(isAsyncTaskSubmission({ task_id: 12, status: 'pending' })).toBe(true)
    expect(isAsyncTaskSubmission({ task_id: 0, status: 'pending' })).toBe(false)
    expect(isAsyncTaskSubmission({ synced_count: 3 })).toBe(false)
    expect(isAsyncTaskSubmission(null)).toBe(false)
  })

  it('polls until task status becomes completed and forwards progress', async () => {
    const onProgress = vi.fn()
    vi.mocked(http.get)
      .mockResolvedValueOnce({ id: 9, status: 'running', progress: 40, message: '执行中', result: null })
      .mockResolvedValueOnce({ id: 9, status: 'completed', progress: 100, message: '已完成', result: { synced_count: 2 } })

    const res = await pollTask<{ synced_count: number }>(9, { intervalMs: 1, timeoutMs: 2000, onProgress })
    expect(res).toEqual({ synced_count: 2 })
    expect(onProgress).toHaveBeenCalledTimes(2)
  })

  it('throws server error_message when async task fails', async () => {
    vi.mocked(http.get).mockResolvedValueOnce({
      id: 10,
      status: 'failed',
      progress: 0,
      message: '失败',
      error_message: '上游连接超时',
      result: null,
    })

    await expect(pollTask(10, { intervalMs: 1, timeoutMs: 2000 })).rejects.toThrow('上游连接超时')
  })

  it('supports synchronous direct response in resolveMaybeAsync', async () => {
    const direct = await resolveMaybeAsync({ status: 'ok', mode: 'l2' })
    expect(direct).toEqual({ status: 'ok', mode: 'l2' })
    expect(http.get).not.toHaveBeenCalled()
  })

  it('polls receipt OCR scan tasks via receiptsApi.scan', async () => {
    vi.mocked(http.post).mockResolvedValueOnce({
      task_id: 31,
      task_type: 'receipt_scan',
      status: 'pending',
      deduplicated: false,
    })
    vi.mocked(http.get).mockResolvedValueOnce({
      id: 31,
      status: 'completed',
      progress: 100,
      message: '已完成',
      result: {
        date: '2026-11-21',
        amount: 68.0,
        type: 'expense',
        category_id: 1,
        category_name: '餐饮美食',
        counterparty: '离线智能识图凭据',
        description: '本地票据/发票',
        currency: 'CNY',
        confidence: 0.85,
      },
    })

    const res = await receiptsApi.scan({ image: 'data:image/png;base64,AAA=' })
    expect(http.post).toHaveBeenCalledWith('/receipts/scan', { image: 'data:image/png;base64,AAA=' })
    expect(http.get).toHaveBeenCalledWith('/tasks/31')
    expect(res.amount).toBe(68.0)
  })

  it('polls CSV import tasks via transactionsApi.importCsv and dailyExpensesApi.importCsv', async () => {
    vi.mocked(http.post)
      .mockResolvedValueOnce({
        task_id: 41,
        task_type: 'import_transactions',
        status: 'pending',
        deduplicated: false,
      })
      .mockResolvedValueOnce({
        task_id: 42,
        task_type: 'import_daily_expenses',
        status: 'pending',
        deduplicated: false,
      })
    vi.mocked(http.get)
      .mockResolvedValueOnce({
        id: 41,
        status: 'completed',
        progress: 100,
        message: '已完成',
        result: { imported: 2, errors: 0, matched_rules: 1 },
      })
      .mockResolvedValueOnce({
        id: 42,
        status: 'completed',
        progress: 100,
        message: '已完成',
        result: { imported: 5, errors: 1, errors_detail: '第6行: 字段数不足' },
      })

    const txRes = await transactionsApi.importCsv('date,asset_name\n')
    expect(txRes.imported).toBe(2)

    const expRes = await dailyExpensesApi.importCsv('date,asset_name\n')
    expect(expRes.imported).toBe(5)
    expect(expRes.errors).toBe(1)
  })

  it('polls SQLite hot backup tasks via backupApi.createBackup', async () => {
    vi.mocked(http.post).mockResolvedValueOnce({
      task_id: 51,
      task_type: 'system_backup_create',
      status: 'pending',
      deduplicated: false,
    })
    vi.mocked(http.get).mockResolvedValueOnce({
      id: 51,
      status: 'completed',
      progress: 100,
      message: '已完成',
      result: { filename: 'minefolio_backup_20261121_120000.db', size: 65536, created_at: 1700000000 },
    })

    const res = await backupApi.createBackup()
    expect(http.post).toHaveBeenCalledWith('/system/backup/create')
    expect(http.get).toHaveBeenCalledWith('/tasks/51')
    expect(res.filename).toBe('minefolio_backup_20261121_120000.db')
  })

  it('polls ledger rebuild tasks via assetsApi.rebuildSingle and assetsApi.rebuildAll', async () => {
    vi.mocked(http.post)
      .mockResolvedValueOnce({
        task_id: 61,
        task_type: 'asset_rebuild_single',
        status: 'pending',
        deduplicated: false,
      })
      .mockResolvedValueOnce({
        task_id: 62,
        task_type: 'asset_rebuild_all',
        status: 'pending',
        deduplicated: false,
      })
    vi.mocked(http.get)
      .mockResolvedValueOnce({
        id: 61,
        status: 'completed',
        progress: 100,
        message: '已完成',
        result: { asset_id: 10, balance: '5000.00', tx_count: 4 },
      })
      .mockResolvedValueOnce({
        id: 62,
        status: 'completed',
        progress: 100,
        message: '已完成',
        result: { status: 'rebuilt' },
      })

    const single = await assetsApi.rebuildSingle(10)
    expect(single.asset_id).toBe(10)

    const all = await assetsApi.rebuildAll()
    expect(all.status).toBe('rebuilt')
  })

  it('polls MCP probe/refresh and L3 skill test tasks seamlessly', async () => {
    vi.mocked(http.post)
      .mockResolvedValueOnce({
        task_id: 71,
        task_type: 'mcp_server_refresh',
        status: 'pending',
        deduplicated: false,
      })
      .mockResolvedValueOnce({
        task_id: 72,
        task_type: 'mcp_server_refresh',
        status: 'pending',
        deduplicated: true,
      })
      .mockResolvedValueOnce({
        task_id: 73,
        task_type: 'skill_test',
        status: 'pending',
        deduplicated: false,
      })
    vi.mocked(http.get)
      .mockResolvedValueOnce({
        id: 71,
        status: 'completed',
        progress: 100,
        message: '已完成',
        result: { server_id: 5, status: 'ok', tool_count: 3 },
      })
      .mockResolvedValueOnce({
        id: 72,
        status: 'completed',
        progress: 100,
        message: '已完成',
        result: { server_id: 5, status: 'ok', tool_count: 3 },
      })
      .mockResolvedValueOnce({
        id: 73,
        status: 'completed',
        progress: 100,
        message: '已完成',
        result: { status: 'success', mode: 'l3', output: 'ok' },
      })

    const probe = await testMcpServer(5)
    expect(probe.status).toBe('ok')
    expect(probe.tool_count).toBe(3)

    const refreshed = await refreshMcpServer(5)
    expect(refreshed.tool_count).toBe(3)

    const l3 = await testSkill(9, '{"x":1}')
    expect(l3.status).toBe('success')
    expect(l3.mode).toBe('l3')
  })
})
