import { describe, it, expect, vi, beforeEach } from 'vitest'

const { postMock, putMock, getMock } = vi.hoisted(() => ({
  postMock: vi.fn(),
  putMock: vi.fn(),
  getMock: vi.fn()
}))

vi.mock('@/utils/http', () => ({
  default: {
    get: getMock,
    post: postMock,
    put: putMock,
    delete: vi.fn()
  }
}))

import { schedulerApi } from '@/api/scheduler'
import { classifyIlinkChannelStatus } from '@/utils/ilink-channel'

describe('scheduler wechat_ilink channel', () => {
  beforeEach(() => {
    postMock.mockReset()
    putMock.mockReset()
    getMock.mockReset()
  })

  it('saves and tests wechat_ilink channel with empty url/secret', async () => {
    putMock.mockResolvedValue({ message: 'ok' })
    postMock.mockResolvedValue({ message: 'ok' })
    await schedulerApi.saveChannel({
      channel_type: 'wechat_ilink',
      channel_name: '微信 iLink 机器人',
      webhook_url: ''
    })
    expect(putMock).toHaveBeenCalledWith('/scheduler/channel', {
      channel_type: 'wechat_ilink',
      channel_name: '微信 iLink 机器人',
      webhook_url: ''
    })

    await schedulerApi.testChannel()
    expect(postMock).toHaveBeenCalledWith('/scheduler/channel/test')
  })

  it('classifies ilink readiness across unbound, inactive, missing/stale inbound, and ready', () => {
    const now = Date.parse('2026-11-25T12:00:00Z')

    expect(classifyIlinkChannelStatus(undefined, now)).toEqual({
      level: 'danger',
      reason: 'not_bound'
    })
    expect(classifyIlinkChannelStatus({ has_instance: false }, now)).toEqual({
      level: 'danger',
      reason: 'not_bound'
    })
    expect(
      classifyIlinkChannelStatus(
        { has_instance: true, enabled: false, status: 'disabled' },
        now
      )
    ).toEqual({ level: 'warning', reason: 'disabled' })
    expect(
      classifyIlinkChannelStatus(
        { has_instance: true, enabled: true, status: 'session_expired' },
        now
      )
    ).toEqual({ level: 'warning', reason: 'session_expired' })
    expect(
      classifyIlinkChannelStatus(
        { has_instance: true, enabled: true, status: 'active', last_inbound_at: null },
        now
      )
    ).toEqual({ level: 'warning', reason: 'needs_inbound' })
    expect(
      classifyIlinkChannelStatus(
        {
          has_instance: true,
          enabled: true,
          status: 'active',
          last_inbound_at: '2026-11-24T10:00:00Z' // 26h ago > 20h
        },
        now
      )
    ).toEqual({ level: 'warning', reason: 'needs_inbound' })
    expect(
      classifyIlinkChannelStatus(
        {
          has_instance: true,
          enabled: true,
          status: 'active',
          last_inbound_at: '2026-11-25T10:00:00Z' // 2h ago
        },
        now
      )
    ).toEqual({ level: 'success', reason: 'ready' })
  })
})
