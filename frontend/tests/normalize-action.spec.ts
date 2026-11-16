import { describe, it, expect } from 'vitest'
import { normalizeActionCard } from '@/utils/normalize-action'

describe('normalizeActionCard', () => {
  it('converts flat daily_expense payload into nested ProposedAction', () => {
    const flat = {
      action_type: 'daily_expense',
      amount: 86.5,
      type: 'expense',
      category_id: 12,
      category_name: '日常购物',
      asset_id: 3,
      asset_name: '微信零钱',
      date: '2026-11-16',
      note: '超市购物',
      draft_token: 'token_abc_123',
    }
    const normalized = normalizeActionCard(flat)
    expect(normalized.action_type).toBe('daily_expense')
    expect(normalized.data).toBeDefined()
    expect(normalized.data.amount).toBe(86.5)
    expect(normalized.data.category_name).toBe('日常购物')
    expect(normalized.data.draft_token).toBe('token_abc_123')
  })

  it('converts flat transfer payload into nested ProposedAction', () => {
    const flat = {
      action_type: 'transfer',
      amount: 500,
      from_asset_id: 1,
      from_asset_name: '招商银行',
      to_asset_id: 2,
      to_asset_name: '微信零钱',
      date: '2026-11-16',
      fee: 0,
      note: '转账',
      draft_token: 'token_xyz_456',
    }
    const normalized = normalizeActionCard(flat)
    expect(normalized.action_type).toBe('transfer')
    expect(normalized.data.amount).toBe(500)
    expect(normalized.data.from_asset_name).toBe('招商银行')
    expect(normalized.data.to_asset_name).toBe('微信零钱')
    expect(normalized.data.draft_token).toBe('token_xyz_456')
  })

  it('preserves already nested ProposedAction structure without double-nesting', () => {
    const nested = {
      action_type: 'daily_expense',
      status: 'pending',
      data: {
        amount: 100,
        type: 'income',
        category_id: 5,
        draft_token: 'token_nested_789',
      },
    } as any
    const normalized = normalizeActionCard(nested)
    expect(normalized.action_type).toBe('daily_expense')
    expect(normalized.data.amount).toBe(100)
    expect(normalized.data.type).toBe('income')
    expect(normalized.data.draft_token).toBe('token_nested_789')
  })
})
