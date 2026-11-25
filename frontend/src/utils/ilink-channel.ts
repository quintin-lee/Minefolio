import type { IlinkChannelStatus } from '@/types/scheduler'

export interface IlinkReadiness {
  level: 'success' | 'warning' | 'danger'
  reason: string
}

const MAX_CONTEXT_AGE_MS = 20 * 60 * 60 * 1000

const ILINK_STATUS_LABELS: Record<string, string> = {
  ready: '已就绪',
  not_bound: '未创建',
  needs_inbound: '待发消息激活',
  pending_login: '等待扫码',
  qr_ready: '等待扫码',
  scanned: '已扫码待确认',
  need_verifycode: '需安全验证',
  disabled: '已停用',
  session_expired: '登录态过期',
  expired: '登录态过期',
  error: '异常'
}

export function parseIlinkTimestampMs(ts?: string | null): number {
  if (!ts || typeof ts !== 'string') {
    return NaN
  }
  const trimmed = ts.trim()
  if (!trimmed) {
    return NaN
  }

  let normalized = trimmed
  if (/^\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}:\d{2}(\.\d+)?$/.test(trimmed)) {
    normalized = trimmed.replace(' ', 'T') + 'Z'
  } else {
    normalized = trimmed
      .replace(/^(\d{4}-\d{2}-\d{2})\s+/, '$1T')
      .replace(/([+-]\d{2})$/, '$1:00')
      .replace(/([+-]\d{2})(\d{2})$/, '$1:$2')
  }

  const ms = Date.parse(normalized)
  return Number.isFinite(ms) ? ms : NaN
}

export function getIlinkStatusLabel(reason: string): string {
  return ILINK_STATUS_LABELS[reason] ?? reason
}

export function formatIlinkInboundTime(ts?: string | null): string {
  if (!ts) {
    return '-'
  }
  const ms = parseIlinkTimestampMs(ts)
  if (!Number.isFinite(ms)) {
    return ts
  }
  return new Date(ms).toLocaleString()
}

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
      reason:
        !st.status || st.status === 'disabled' || st.status === 'active'
          ? 'disabled'
          : st.status
    }
  }
  if (st.status !== 'active') {
    return { level: 'warning', reason: st.status || 'pending_login' }
  }
  if (!st.last_inbound_at) {
    return { level: 'warning', reason: 'needs_inbound' }
  }
  const inboundMs = parseIlinkTimestampMs(st.last_inbound_at)
  if (!Number.isFinite(inboundMs) || nowMs - inboundMs > MAX_CONTEXT_AGE_MS) {
    return { level: 'warning', reason: 'needs_inbound' }
  }
  return { level: 'success', reason: 'ready' }
}
