import { describe, it, expect, beforeEach } from 'vitest'
import { readFileSync } from 'node:fs'
import { resolve } from 'node:path'
import { buildOAuthAuthUrl, consumeAndVerifyOAuthState } from '@/utils/oauth-state'

describe('Task 1 Security Hardening', () => {
  beforeEach(() => {
    sessionStorage.clear()
  })

  it('NewsPulse sanitizes rendered markdown via DOMPurify / renderSafeMarkdown', () => {
    const src = readFileSync(resolve(process.cwd(), 'src/views/NewsPulse.vue'), 'utf8')
    const mdSrc = readFileSync(resolve(process.cwd(), 'src/utils/markdown.ts'), 'utf8')
    expect(src).toMatch(/renderSafeMarkdown|DOMPurify\.sanitize/)
    expect(mdSrc).toMatch(/import DOMPurify from 'dompurify'/)
    expect(mdSrc).toMatch(/DOMPurify\.sanitize/)
  })

  it('AiTraceDetail escapes HTML in markdown fallback <pre>', () => {
    const src = readFileSync(resolve(process.cwd(), 'src/views/AiTraceDetail.vue'), 'utf8')
    const mdSrc = readFileSync(resolve(process.cwd(), 'src/utils/markdown.ts'), 'utf8')
    expect(src).not.toMatch(/return `<pre>\$\{content\}<\/pre>`/)
    expect(src + mdSrc).toMatch(/replace\(\/&\/g,\s*'&amp;'\)/)
  })

  it('main-mobile renders bootstrap errors via textContent instead of innerHTML', () => {
    const src = readFileSync(resolve(process.cwd(), 'src/main-mobile.ts'), 'utf8')
    expect(src).not.toMatch(/root\.innerHTML\s*=/)
    expect(src).toMatch(/textContent/)
    expect(src).toMatch(/replaceChildren/)
  })

  it('oauth-state attaches single-use CSRF state and rejects mismatched or missing state', () => {
    const redirectUrl = buildOAuthAuthUrl('https://github.com/login/oauth/authorize?client_id=abc')
    const parsed = new URL(redirectUrl)
    const state = parsed.searchParams.get('state') || ''
    expect(state.length).toBeGreaterThanOrEqual(16)

    expect(consumeAndVerifyOAuthState('wrong-state')).toBe(false)
    // Once consumed, even the original state cannot be replayed
    expect(consumeAndVerifyOAuthState(state)).toBe(false)

    const redirectUrl2 = buildOAuthAuthUrl('https://github.com/login/oauth/authorize?client_id=abc')
    const state2 = new URL(redirectUrl2).searchParams.get('state') || ''
    expect(consumeAndVerifyOAuthState(state2)).toBe(true)
    expect(consumeAndVerifyOAuthState(state2)).toBe(false)
  })
})
