import { describe, it, expect } from 'vitest'
import { readFileSync } from 'node:fs'
import { resolve } from 'node:path'

const SHEETS = [
  'AssetSheet.vue',
  'AccountSheet.vue',
  'CategorySheet.vue',
  'TagSheet.vue',
  'TransactionSheet.vue',
]

function src(name: string): string {
  return readFileSync(resolve(process.cwd(), `src/components/mobile/${name}`), 'utf8')
}

describe('mobile write sheets', () => {
  it.each(SHEETS)('%s uses offlineApi and the modelValue contract', (name) => {
    const code = src(name)
    expect(code).toContain("from '@/utils/offline-http'")
    expect(code).toMatch(/offlineApi\.(post|put)\(/)
    expect(code).toContain("(e: 'update:modelValue', v: boolean): void")
    expect(code).toContain("(e: 'saved'): void")
  })

  it.each(SHEETS)('%s keeps the form open on online failure', (name) => {
    const code = src(name)
    const catchBlock = code.slice(code.indexOf('catch (err'), code.indexOf('finally'))
    expect(catchBlock).toContain('ElMessage.error')
    expect(catchBlock).not.toContain('visible.value = false')
  })

  it('TransactionSheet never writes direction into the payload', () => {
    const code = src('TransactionSheet.vue')
    expect(code).not.toMatch(/payload\.direction\s*=/)
    expect(code).not.toMatch(/direction:\s*form\./)
    expect(code).toContain("不写入 direction")
  })

  it('TransactionSheet covers the full TransactionInput writable fields', () => {
    const code = src('TransactionSheet.vue')
    for (const field of [
      'asset_id', 'linked_asset_id', 'category_id', 'transaction_type', 'amount',
      'price_per_unit', 'quantity', 'fee', 'currency', 'transaction_date', 'note', 'source_type',
    ]) {
      expect(code).toContain(field)
    }
  })

  it('AssetSheet binds account_id and CategorySheet binds parent_id', () => {
    expect(src('AssetSheet.vue')).toContain('payload.account_id = form.account_id')
    expect(src('CategorySheet.vue')).toContain('payload.parent_id = form.parent_id')
  })
})
