import { describe, it, expect, vi, beforeEach } from 'vitest'

const { httpMock, toDataURLMock } = vi.hoisted(() => ({
  httpMock: {
    get: vi.fn(),
    post: vi.fn(),
    delete: vi.fn(),
  },
  toDataURLMock: vi.fn(async (text: string) => `data:image/png;base64,MOCK(${text})`),
}))

vi.mock('@/utils/http', () => ({
  default: httpMock,
}))

vi.mock('qrcode', () => ({
  default: {
    toDataURL: toDataURLMock,
  },
}))

import { imApi, renderQrContentToImage } from '@/api/im'

describe('imApi & iLink QR rendering', () => {
  beforeEach(() => {
    vi.clearAllMocks()
  })

  it('calls the expected iLink instance endpoints', async () => {
    httpMock.get.mockResolvedValue([])
    httpMock.post.mockResolvedValue({ id: 1 })
    httpMock.delete.mockResolvedValue(null)

    await imApi.listInstances()
    expect(httpMock.get).toHaveBeenCalledWith('/im/instances')

    await imApi.getInstance(7)
    expect(httpMock.get).toHaveBeenCalledWith('/im/instances/7')

    await imApi.createInstance()
    expect(httpMock.post).toHaveBeenCalledWith('/im/instances', {
      platform: 'wechat',
      adapter: 'ilink',
    })

    await imApi.reloginInstance(7)
    expect(httpMock.post).toHaveBeenCalledWith('/im/instances/7/relogin')

    await imApi.verifyInstanceCode(7, '123456')
    expect(httpMock.post).toHaveBeenCalledWith('/im/instances/7/verify-code', {
      code: '123456',
    })

    await imApi.enableInstance(7)
    expect(httpMock.post).toHaveBeenCalledWith('/im/instances/7/enable')

    await imApi.disableInstance(7)
    expect(httpMock.post).toHaveBeenCalledWith('/im/instances/7/disable')

    await imApi.deleteInstance(7)
    expect(httpMock.delete).toHaveBeenCalledWith('/im/instances/7')
  })

  it('passes through data image URIs and encodes URL/raw QR payloads via qrcode', async () => {
    expect(await renderQrContentToImage('   ')).toBe('')
    expect(await renderQrContentToImage('data:image/png;base64,AAA')).toBe(
      'data:image/png;base64,AAA'
    )
    expect(toDataURLMock).not.toHaveBeenCalled()

    const ilinkUrl = 'https://liteapp.weixin.qq.com/q/7GiQu1?qrcode=abc&bot_type=3'
    const encodedUrl = await renderQrContentToImage(ilinkUrl)
    expect(toDataURLMock).toHaveBeenCalledWith(ilinkUrl, { width: 260, margin: 1 })
    expect(encodedUrl).toBe(`data:image/png;base64,MOCK(${ilinkUrl})`)

    const encodedRaw = await renderQrContentToImage('raw-ilink-qr-payload')
    expect(toDataURLMock).toHaveBeenCalledWith('raw-ilink-qr-payload', { width: 260, margin: 1 })
    expect(encodedRaw).toBe('data:image/png;base64,MOCK(raw-ilink-qr-payload)')
  })
})
