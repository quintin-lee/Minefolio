import { describe, it, expect } from 'vitest'
import { zhCN } from '@/locales/zh-CN'
import { enUS } from '@/locales/en-US'

type Node = { [k: string]: Node | string }

function assertKeysEqual(a: Node, b: Node, path = ''): void {
  const ak = Object.keys(a).sort()
  const bk = Object.keys(b).sort()
  expect(bk, `en-US key mismatch at: ${path || '(root)'}`).toEqual(ak)
  for (const k of ak) {
    const p = path ? `${path}.${k}` : k
    const av = a[k]
    const bv = b[k]
    if (typeof av === 'string') {
      expect(typeof bv, `en-US value not a string at: ${p}`).toBe('string')
      const paramsA = [...av.matchAll(/\{(\w+)\}/g)].map(m => m[1]).sort()
      const paramsB = [...(bv as string).matchAll(/\{(\w+)\}/g)].map(m => m[1]).sort()
      expect(paramsB, `interpolation params differ at: ${p}`).toEqual(paramsA)
    } else {
      expect(typeof bv, `zh value is object but en-US is not at: ${p}`).toBe('object')
      assertKeysEqual(av as Node, bv as Node, p)
    }
  }
}

describe('i18n dictionaries', () => {
  it('en-US key tree matches zh-CN exactly (incl. interpolation params)', () => {
    assertKeysEqual(zhCN as unknown as Node, enUS as unknown as Node)
  })

  it('compiles all messages without compilation syntax errors', async () => {
    const { createI18n } = await import('vue-i18n')
    const i18n = createI18n({
      legacy: false,
      locale: 'zh-CN',
      messages: {
        'zh-CN': zhCN,
        'en-US': enUS,
      },
    })

    function checkNode(node: Node, path = '') {
      for (const k of Object.keys(node)) {
        const p = path ? `${path}.${k}` : k
        const v = node[k]
        if (typeof v === 'string') {
          // Extract params if any
          const paramMatches = [...v.matchAll(/\{(\w+)\}/g)].map(m => m[1])
          const params: Record<string, string> = {}
          paramMatches.forEach(name => { params[name] = '1' })
          expect(() => i18n.global.t(p, params), `Failed to compile key: ${p}`).not.toThrow()
        } else {
          checkNode(v as Node, p)
        }
      }
    }

    checkNode(zhCN as unknown as Node)
    i18n.global.locale.value = 'en-US'
    checkNode(enUS as unknown as Node)
  })
})
