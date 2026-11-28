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

  it('normalizes new proposal action types (create_category, create_tag, savings_goal, transaction)', () => {
    const cat = normalizeActionCard({
      action_type: 'create_category',
      name: '宠物消费',
      type: 'expense',
      draft_token: 'tok_cat',
    })
    expect(cat.action_type).toBe('create_category')
    expect(cat.data.name).toBe('宠物消费')
    expect(cat.data.draft_token).toBe('tok_cat')

    const tag = normalizeActionCard({
      action_type: 'create_tag',
      name: '出差报销',
      color: '#10b981',
      draft_token: 'tok_tag',
    })
    expect(tag.action_type).toBe('create_tag')
    expect(tag.data.name).toBe('出差报销')

    const goal = normalizeActionCard({
      action_type: 'savings_goal',
      name: '备用金',
      target_amount: 50000,
      draft_token: 'tok_goal',
    })
    expect(goal.action_type).toBe('savings_goal')
    expect(goal.data.target_amount).toBe(50000)

    const tx = normalizeActionCard({
      action_type: 'transaction',
      transaction_type: 'buy',
      amount: 1000,
      asset_id: 103,
      draft_token: 'tok_tx',
    })
    expect(tx.action_type).toBe('transaction')
    expect(tx.data.transaction_type).toBe('buy')
    expect(tx.data.amount).toBe(1000)
  })
})
