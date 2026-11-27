// @vitest-environment jsdom
import { describe, it, expect, vi, beforeEach } from 'vitest'
import { mount, flushPromises } from '@vue/test-utils'
import ElementPlus, { ElMessage } from 'element-plus'

const { postMock, putMock, getMock, pushMock } = vi.hoisted(() => ({
  postMock: vi.fn(),
  putMock: vi.fn(),
  getMock: vi.fn(),
  pushMock: vi.fn()
}))

vi.mock('@/utils/http', () => ({
  default: {
    get: getMock,
    post: postMock,
    put: putMock,
    delete: vi.fn()
  }
}))

vi.mock('vue-router', () => ({
  useRouter: () => ({
    push: pushMock
  })
}))

import { schedulerApi } from '@/api/scheduler'
import {
  classifyIlinkChannelStatus,
  formatIlinkInboundTime,
  getIlinkStatusLabel,
  parseIlinkTimestampMs
} from '@/utils/ilink-channel'
import ChannelConfigDialog from '@/components/scheduler/ChannelConfigDialog.vue'
import JobLogDrawer from '@/components/scheduler/JobLogDrawer.vue'
import Scheduler from '@/views/Scheduler.vue'

describe('scheduler wechat_ilink channel', () => {
  beforeEach(() => {
    postMock.mockReset()
    putMock.mockReset()
    getMock.mockReset()
    pushMock.mockReset()
    vi.restoreAllMocks()
  })

  it('saves and tests wechat_ilink channel with empty url/secret and 20s timeout + abort signal', async () => {
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
    expect(postMock).toHaveBeenCalledWith('/scheduler/channel/test', undefined, {
      timeout: 20000,
      signal: undefined
    })

    const controller = new AbortController()
    await schedulerApi.testChannel({ signal: controller.signal })
    expect(postMock).toHaveBeenLastCalledWith('/scheduler/channel/test', undefined, {
      timeout: 20000,
      signal: controller.signal
    })
  })

  it('parses ISO, PostgreSQL TIMESTAMPTZ, and timezone-less UTC timestamps cleanly', () => {
    const expected = Date.parse('2026-11-25T15:00:00Z')
    expect(parseIlinkTimestampMs('2026-11-25T15:00:00Z')).toBe(expected)
    expect(parseIlinkTimestampMs('2026-11-25 15:00:00+00')).toBe(expected)
    expect(parseIlinkTimestampMs('2026-11-25 23:00:00+0800')).toBe(expected)
    expect(parseIlinkTimestampMs('2026-11-25 23:00:00+08')).toBe(expected)
    expect(parseIlinkTimestampMs('2026-11-25 15:00:00.250+00')).toBe(expected + 250)
    expect(parseIlinkTimestampMs('2026-11-25 15:00:00')).toBe(expected)
    expect(Number.isNaN(parseIlinkTimestampMs(''))).toBe(true)
    expect(Number.isNaN(parseIlinkTimestampMs('not-a-timestamp'))).toBe(true)
    expect(Number.isNaN(parseIlinkTimestampMs(null))).toBe(true)
    expect(Number.isNaN(parseIlinkTimestampMs(undefined))).toBe(true)
  })

  it('classifies ilink readiness across unbound, inactive, boundary, invalid/stale inbound, and ready', () => {
    const now = Date.parse('2026-11-25T20:00:00Z')

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
        { has_instance: true, enabled: false, status: 'active' },
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
        { has_instance: true, enabled: true, status: 'active', last_inbound_at: 'invalid-date' },
        now
      )
    ).toEqual({ level: 'warning', reason: 'needs_inbound' })

    // Exact 20h boundary: <= 20h is ready, > 20h is needs_inbound
    expect(
      classifyIlinkChannelStatus(
        {
          has_instance: true,
          enabled: true,
          status: 'active',
          last_inbound_at: '2026-11-25 00:00:00+00' // exactly 20h ago
        },
        now
      )
    ).toEqual({ level: 'success', reason: 'ready' })
    expect(
      classifyIlinkChannelStatus(
        {
          has_instance: true,
          enabled: true,
          status: 'active',
          last_inbound_at: '2026-11-24T23:59:59.999Z' // 20h + 1ms ago
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
          last_inbound_at: '2026-11-25 18:00:00+0800' // 10h ago UTC (10:00Z)
        },
        now
      )
    ).toEqual({ level: 'success', reason: 'ready' })
    expect(
      classifyIlinkChannelStatus(
        {
          has_instance: true,
          enabled: true,
          status: 'active',
          last_inbound_at: '2026-11-25 18:00:00' // timezone-less UTC, 2h ago
        },
        now
      )
    ).toEqual({ level: 'success', reason: 'ready' })
  })

  it('localizes ilink status labels and formats inbound timestamps safely', () => {
    expect(getIlinkStatusLabel('ready')).toBe('已就绪')
    expect(getIlinkStatusLabel('not_bound')).toBe('未创建')
    expect(getIlinkStatusLabel('needs_inbound')).toBe('待发消息激活')
    expect(getIlinkStatusLabel('pending_login')).toBe('等待扫码')
    expect(getIlinkStatusLabel('qr_ready')).toBe('等待扫码')
    expect(getIlinkStatusLabel('scanned')).toBe('已扫码待确认')
    expect(getIlinkStatusLabel('need_verifycode')).toBe('需安全验证')
    expect(getIlinkStatusLabel('disabled')).toBe('已停用')
    expect(getIlinkStatusLabel('session_expired')).toBe('登录态过期')
    expect(getIlinkStatusLabel('expired')).toBe('登录态过期')
    expect(getIlinkStatusLabel('error')).toBe('异常')
    expect(getIlinkStatusLabel('custom_state')).toBe('custom_state')

    expect(formatIlinkInboundTime(null)).toBe('-')
    expect(formatIlinkInboundTime('bad-ts')).toBe('bad-ts')
    expect(formatIlinkInboundTime('2026-11-25T15:00:00Z')).toBe(
      new Date('2026-11-25T15:00:00Z').toLocaleString()
    )
  })

  describe('ChannelConfigDialog.vue', () => {
    it('shows ready state with bot ID and formatted inbound time while hiding secret/webhook fields', async () => {
      const recentIso = new Date(Date.now() - 30 * 60 * 1000).toISOString()
      getMock.mockResolvedValue({
        channel_type: 'wechat_ilink',
        channel_name: '我的 iLink 微信',
        webhook_url: '',
        secret_key_masked: '',
        is_active: 1,
        ilink_status: {
          has_instance: true,
          instance_id: 12,
          status: 'active',
          enabled: true,
          ilink_bot_id: 'bot_ready@im.bot',
          last_inbound_at: recentIso
        }
      })

      const wrapper = mount(ChannelConfigDialog, {
        global: {
          plugins: [ElementPlus],
          stubs: { Icon: true }
        }
      })

      await (wrapper.vm as any).open()
      await flushPromises()

      expect(wrapper.text()).toContain('微信 iLink 实例已就绪')
      expect(wrapper.text()).toContain('bot_ready@im.bot')
      expect(wrapper.text()).toContain(formatIlinkInboundTime(recentIso))
      expect(wrapper.text()).not.toContain('SendKey')
      expect(wrapper.text()).not.toContain('Webhook 地址')
      expect(wrapper.text()).not.toContain('前往设置绑定微信机器人')

      wrapper.unmount()
    })

    it('hides bind button on needs_inbound and navigates to /settings?tab=im-bot on session_expired/not_bound', async () => {
      getMock.mockResolvedValueOnce({
        channel_type: 'wechat_ilink',
        channel_name: 'iLink',
        is_active: 1,
        ilink_status: {
          has_instance: true,
          enabled: true,
          status: 'active',
          last_inbound_at: null
        }
      })

      const wrapper = mount(ChannelConfigDialog, {
        global: {
          plugins: [ElementPlus],
          stubs: { Icon: true }
        }
      })

      await (wrapper.vm as any).open()
      await flushPromises()
      expect(wrapper.text()).toContain('激活推送上下文')
      expect(wrapper.text()).not.toContain('前往设置绑定微信机器人')

      getMock.mockResolvedValueOnce({
        channel_type: 'wechat_ilink',
        channel_name: 'iLink',
        is_active: 1,
        ilink_status: {
          has_instance: true,
          enabled: true,
          status: 'session_expired',
          last_inbound_at: null
        }
      })

      await (wrapper.vm as any).open()
      await flushPromises()
      expect(wrapper.text()).toContain('登录态过期')
      expect(wrapper.text()).not.toContain('session_expired')

      const bindBtn = wrapper
        .findAll('button')
        .find((b) => b.text().includes('前往设置绑定微信机器人'))
      expect(bindBtn).toBeDefined()
      await bindBtn!.trigger('click')
      expect(pushMock).toHaveBeenCalledWith({ path: '/settings', query: { tab: 'im-bot' } })

      wrapper.unmount()
    })

    it('halts handleTest silently when form validation fails or warns when secret key is missing', async () => {
      const errSpy = vi.spyOn(ElMessage, 'error').mockImplementation(() => ({}) as any)
      const warnSpy = vi.spyOn(ElMessage, 'warning').mockImplementation(() => ({}) as any)

      getMock.mockResolvedValue({
        channel_type: 'serverchan',
        channel_name: 'Server酱',
        webhook_url: '',
        secret_key_masked: '',
        is_active: 0
      })

      let rejectNextValidate = true
      const wrapper = mount(ChannelConfigDialog, {
        global: {
          plugins: [ElementPlus],
          stubs: {
            Icon: true,
            'el-form': {
              methods: {
                validate: () =>
                  rejectNextValidate
                    ? Promise.reject({ channel_name: [{ message: '请输入通道备注名称' }] })
                    : Promise.resolve(true),
                clearValidate: () => {}
              },
              template: '<form><slot /></form>'
            }
          }
        }
      })

      await (wrapper.vm as any).open()
      await flushPromises()

      const testBtn = wrapper.findAll('button').find((b) => b.text().includes('发送测试消息'))
      expect(testBtn).toBeDefined()

      // Case 1: Form validation fails -> silent abort, no API call, no ElMessage.error
      await testBtn!.trigger('click')
      await flushPromises()
      expect(putMock).not.toHaveBeenCalled()
      expect(postMock).not.toHaveBeenCalled()
      expect(errSpy).not.toHaveBeenCalled()

      // Case 2: Form validates, serverchan secret key empty -> ElMessage.warning, no API call
      rejectNextValidate = false
      await testBtn!.trigger('click')
      await flushPromises()
      expect(warnSpy).toHaveBeenCalledWith('请填写推送秘钥 Key')
      expect(putMock).not.toHaveBeenCalled()
      expect(postMock).not.toHaveBeenCalled()

      wrapper.unmount()
    })

    it('saves wechat_ilink with emit(saved), passes AbortSignal, and aborts cleanly on dialog close', async () => {
      const errSpy = vi.spyOn(ElMessage, 'error').mockImplementation(() => ({}) as any)
      const successSpy = vi.spyOn(ElMessage, 'success').mockImplementation(() => ({}) as any)

      getMock.mockResolvedValue({
        channel_type: 'wechat_ilink',
        channel_name: '我的 iLink',
        is_active: 1,
        ilink_status: {
          has_instance: true,
          enabled: true,
          status: 'active',
          ilink_bot_id: 'bot_1',
          last_inbound_at: new Date().toISOString()
        }
      })
      putMock.mockResolvedValue({ message: 'ok' })

      let capturedSignal: AbortSignal | undefined
      postMock.mockImplementation((_url: string, _data: unknown, cfg: { signal?: AbortSignal }) => {
        capturedSignal = cfg?.signal
        return new Promise((_resolve, reject) => {
          cfg?.signal?.addEventListener('abort', () => {
            const abortErr = Object.assign(new Error('canceled'), {
              code: 'ERR_CANCELED',
              name: 'CanceledError'
            })
            reject(abortErr)
          })
        })
      })

      const wrapper = mount(ChannelConfigDialog, {
        global: {
          plugins: [ElementPlus],
          stubs: { Icon: true }
        }
      })

      await (wrapper.vm as any).open()
      await flushPromises()

      const testBtn = wrapper.findAll('button').find((b) => b.text().includes('发送测试消息'))
      expect(testBtn).toBeDefined()
      await testBtn!.trigger('click')
      await flushPromises()

      expect(putMock).toHaveBeenCalledWith('/scheduler/channel', {
        channel_type: 'wechat_ilink',
        channel_name: '我的 iLink',
        webhook_url: '',
        secret_key: undefined
      })
      expect(wrapper.emitted('saved')).toBeTruthy()
      expect(postMock).toHaveBeenCalledWith('/scheduler/channel/test', undefined, {
        timeout: 20000,
        signal: expect.any(AbortSignal)
      })
      expect(capturedSignal?.aborted).toBe(false)

      // Closing dialog aborts in-flight request without showing error toast
      const cancelBtn = wrapper.findAll('button').find((b) => b.text().trim() === '取消')
      expect(cancelBtn).toBeDefined()
      await cancelBtn!.trigger('click')
      await flushPromises()

      expect(capturedSignal?.aborted).toBe(true)
      expect(errSpy).not.toHaveBeenCalled()
      expect(successSpy).not.toHaveBeenCalled()

      wrapper.unmount()
    })
  })

  describe('JobLogDrawer.vue', () => {
    it('renders queued as 待推送 and partial as 部分送达 with warning tags', async () => {
      getMock.mockResolvedValue({
        items: [
          {
            id: 1,
            job_id: 8,
            user_id: 1,
            started_at: '2026-11-25 10:00:00',
            status: 'success',
            duration_ms: 120,
            trigger_reason: 'manual_invoke',
            notification_status: 'queued'
          },
          {
            id: 2,
            job_id: 9,
            user_id: 0,
            started_at: '2026-11-25 10:01:00',
            status: 'success',
            duration_ms: 220,
            trigger_reason: 'cron_tick',
            notification_status: 'partial'
          }
        ],
        total: 2,
        page: 1,
        page_size: 20
      })

      const wrapper = mount(JobLogDrawer, {
        global: {
          plugins: [ElementPlus],
          stubs: { Icon: true }
        }
      })

      ;(wrapper.vm as any).open({ id: 8, name: '测试任务' })
      await flushPromises()

      const warningTags = wrapper.findAll('.el-tag--warning').map((el) => el.text().trim())
      expect(warningTags).toContain('待推送')
      expect(warningTags).toContain('部分送达')

      wrapper.unmount()
    })
  })

  describe('Scheduler.vue', () => {
    it('displays localized iLink readiness tag instead of raw is_active for wechat_ilink channel', async () => {
      getMock
        .mockResolvedValueOnce({
          active_jobs: 1,
          today_runs: 0,
          next_run_job: { name: '', next_run_at: '' },
          push_channel: {
            channel_type: 'wechat_ilink',
            channel_name: '我的 iLink',
            is_active: 1,
            ilink_status: {
              has_instance: true,
              enabled: true,
              status: 'active',
              last_inbound_at: null
            }
          }
        })
        .mockResolvedValueOnce({ items: [], total: 0, page: 1, page_size: 20 })

      const wrapper = mount(Scheduler, {
        global: {
          plugins: [ElementPlus],
          stubs: {
            Icon: true,
            ChannelConfigDialog: true,
            JobDialog: true,
            JobLogDrawer: true
          }
        }
      })

      await flushPromises()
      expect(wrapper.text()).toContain('微信 iLink 机器人')
      const tag = wrapper.find('.channel-tag')
      expect(tag.exists()).toBe(true)
      expect(tag.text().trim()).toBe('待发消息激活')
      expect(tag.find('.el-tag--warning').exists()).toBe(true)

      // Refresh with not_bound -> danger / 未绑定
      getMock
        .mockResolvedValueOnce({
          active_jobs: 1,
          today_runs: 0,
          next_run_job: { name: '', next_run_at: '' },
          push_channel: {
            channel_type: 'wechat_ilink',
            channel_name: '我的 iLink',
            is_active: 1,
            ilink_status: { has_instance: false }
          }
        })
        .mockResolvedValueOnce({ items: [], total: 0, page: 1, page_size: 20 })

      await (wrapper.vm as any).fetchDashboard()
      await flushPromises()
      expect(wrapper.find('.channel-tag').text().trim()).toBe('未绑定')
      expect(wrapper.find('.channel-tag .el-tag--danger').exists()).toBe(true)

      wrapper.unmount()
    })
  })
})
