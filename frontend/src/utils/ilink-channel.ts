import type { IlinkChannelStatus } from '@/types/scheduler'

export interface IlinkReadiness {
  level: 'success' | 'warning' | 'danger'
  reason: string
}

const MAX_CONTEXT_AGE_MS = 20 * 60 * 60 * 1000

export function classifyIlinkChannelStatus(
  st?: IlinkChannelStatus | null,
  nowMs: number = Date.now()
): IlinkReadiness {
  if (!st || !st.has_instance) {
    return { level: 'danger', reason: 'not_bound' }
  }
  if (!st.enabled) {
    return {
      level: 'warning',
      reason: st.status === 'disabled' || !st.status ? 'disabled' : st.status
    }
  }
  if (st.status !== 'active') {
    return { level: 'warning', reason: st.status || 'pending_login' }
  }
  if (!st.last_inbound_at) {
    return { level: 'warning', reason: 'needs_inbound' }
  }
  const inboundMs = Date.parse(st.last_inbound_at)
  if (!Number.isFinite(inboundMs) || nowMs - inboundMs > MAX_CONTEXT_AGE_MS) {
    return { level: 'warning', reason: 'needs_inbound' }
  }
  return { level: 'success', reason: 'ready' }
}
