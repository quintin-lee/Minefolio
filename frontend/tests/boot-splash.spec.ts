import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { readFileSync } from 'node:fs'
import { resolve } from 'node:path'

const { isNativeMock, hideMock } = vi.hoisted(() => ({
  isNativeMock: vi.fn(() => true),
  hideMock: vi.fn(async () => {}),
}))

vi.mock('@capacitor/core', () => ({
  Capacitor: {
    isNativePlatform: isNativeMock,
  },
}))

vi.mock('@capacitor/splash-screen', () => ({
  SplashScreen: {
    hide: hideMock,
  },
}))

import { BOOT_SPLASH_ID, hideNativeSplash, dismissBootSplash } from '@/utils/boot-splash'

describe('mobile boot splash', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    vi.useFakeTimers()
    document.body.innerHTML = ''
  })

  afterEach(() => {
    vi.useRealTimers()
  })

  it('calls SplashScreen.hide only on native platforms and swallows errors', async () => {
    isNativeMock.mockReturnValueOnce(true)
    await hideNativeSplash()
    expect(hideMock).toHaveBeenCalledWith({ fadeOutDuration: 150 })

    hideMock.mockClear()
    isNativeMock.mockReturnValueOnce(false)
    await hideNativeSplash()
    expect(hideMock).not.toHaveBeenCalled()

    isNativeMock.mockReturnValueOnce(true)
    hideMock.mockRejectedValueOnce(new Error('plugin unavailable'))
    await expect(hideNativeSplash()).resolves.toBeUndefined()
  })

  it('fades out and removes #mf-boot-splash on transitionend or timeout fallback', () => {
    const el = document.createElement('div')
    el.id = BOOT_SPLASH_ID
    document.body.appendChild(el)

    dismissBootSplash()
    expect(el.classList.contains('is-leaving')).toBe(true)
    expect(document.getElementById(BOOT_SPLASH_ID)).not.toBeNull()

    el.dispatchEvent(new Event('transitionend'))
    expect(document.getElementById(BOOT_SPLASH_ID)).toBeNull()

    const el2 = document.createElement('div')
    el2.id = BOOT_SPLASH_ID
    document.body.appendChild(el2)
    dismissBootSplash()
    vi.advanceTimersByTime(400)
    expect(document.getElementById(BOOT_SPLASH_ID)).toBeNull()
  })

  it('removes #mf-boot-splash immediately when immediate=true', () => {
    const el = document.createElement('div')
    el.id = BOOT_SPLASH_ID
    document.body.appendChild(el)

    dismissBootSplash(true)
    expect(document.getElementById(BOOT_SPLASH_ID)).toBeNull()
  })

  it('index.mobile.html and main-mobile.ts wire the boot splash handoff', () => {
    const html = readFileSync(resolve(process.cwd(), 'index.mobile.html'), 'utf8')
    const mainMobile = readFileSync(resolve(process.cwd(), 'src/main-mobile.ts'), 'utf8')
    expect(html).toContain(`id="${BOOT_SPLASH_ID}"`)
    expect(mainMobile).toContain('hideNativeSplash')
    expect(mainMobile).toContain('dismissBootSplash')
  })
})
