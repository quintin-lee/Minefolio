import { describe, expect, it } from 'vitest'
import { readFileSync } from 'node:fs'
import { resolve } from 'node:path'

const componentSource = readFileSync(resolve(process.cwd(), 'src/views/AiTraceDetail.vue'), 'utf8')

describe('AiTraceDetail Drawer Redesign', () => {
  it('uses el-drawer instead of cramped el-dialog for wide slide-over workspace', () => {
    expect(componentSource).toMatch(/<el-drawer/i)
    expect(componentSource).not.toMatch(/<el-dialog/i)
  })

  it('includes bento performance and cost metrics bar', () => {
    // 包含性能与成本指标带
    expect(componentSource).toMatch(/class="[^"]*metrics-bar/i)
    expect(componentSource).toMatch(/first_token_ms/i)
    expect(componentSource).toMatch(/tokens_per_sec/i)
    expect(componentSource).toMatch(/cost_usd/i)
  })

  it('provides two-column split workspace for prompt and completion', () => {
    // 包含提示词与回复的分栏工作区
    expect(componentSource).toMatch(/class="[^"]*prompt-pane/i)
    expect(componentSource).toMatch(/class="[^"]*output-pane/i)
  })

  it('supports markdown rendering and raw view mode toggle for model output', () => {
    // 支持 Markdown 渲染与纯文本切换
    expect(componentSource).toMatch(/marked/i)
    expect(componentSource).toMatch(/viewMode|view-toggle|isMarkdown/i)
  })

  it('provides raw json payload inspection and copy functionality', () => {
    // 支持完整 JSON 查看与复制
    expect(componentSource).toMatch(/copyText|handleCopy/i)
    expect(componentSource).toMatch(/raw|json/i)
  })

  it('supports maximize toggle for full-screen inspection', () => {
    // 支持全屏放大查看
    expect(componentSource).toMatch(/isMaximized/i)
  })
})
