import { describe, expect, it } from 'vitest'
import { readFileSync } from 'node:fs'
import { resolve } from 'node:path'

const routerSource = readFileSync(resolve(process.cwd(), 'src/router/index.ts'), 'utf8')
const layoutSource = readFileSync(resolve(process.cwd(), 'src/views/Layout.vue'), 'utf8')

describe('Layout full-bleed support', () => {
  it('configures fullBleed meta flag for /chat route in router', () => {
    // 验证 /chat 路由配置包含 fullBleed: true
    expect(routerSource).toMatch(/path:\s*['"]chat['"][^}]+fullBleed:\s*true/)
  })

  it('binds is-full-bleed class to el-main based on route.meta.fullBleed in Layout.vue', () => {
    // 验证 el-main 动态绑定 is-full-bleed 类名
    expect(layoutSource).toMatch(/<el-main[^>]+is-full-bleed/)
  })

  it('defines .main.is-full-bleed style with zero padding and hidden overflow', () => {
    // 验证 .main.is-full-bleed 的样式规则去除了 padding 并将 overflow 设为 hidden
    expect(layoutSource).toMatch(/\.main\.is-full-bleed\s*{[^}]*padding:\s*0/s)
    expect(layoutSource).toMatch(/\.main\.is-full-bleed\s*{[^}]*overflow:\s*hidden/s)
  })
})
