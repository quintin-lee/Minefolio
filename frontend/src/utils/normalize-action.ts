import type { ProposedAction } from '@/components/ActionCard.vue'

/**
 * 将 LLM 输出的扁平 action 卡片 JSON 归一化为 ActionCard 期望的嵌套结构。
 *
 * LLM 可能输出 { action_type, amount, draft_token, ... }（扁平），
 * 而 ActionCard 读取 actionData.data.amount 等嵌套字段。
 * 若已是嵌套格式则原样返回。
 */
export function normalizeActionCard(raw: Record<string, unknown>): ProposedAction {
  const actionType = (raw.action_type === 'daily_expense' || raw.action_type === 'transfer')
    ? raw.action_type as 'daily_expense' | 'transfer'
    : null

  if (!actionType) {
    return raw as unknown as ProposedAction
  }

  // 已经是嵌套结构 → 直接返回
  if (raw.data && typeof raw.data === 'object') {
    return raw as unknown as ProposedAction
  }

  // 扁平结构 → 将字段收集进 data，保留 action_type/status 在顶层
  const data: Record<string, unknown> = {}
  const rest: Record<string, unknown> = {}
  for (const [k, v] of Object.entries(raw)) {
    if (k === 'action_type' || k === 'status') {
      rest[k] = v
    } else {
      data[k] = v
    }
  }
  rest.data = data
  return rest as unknown as ProposedAction
}
