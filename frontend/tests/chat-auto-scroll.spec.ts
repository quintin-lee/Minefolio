import { describe, expect, it } from 'vitest'
import { readFileSync } from 'node:fs'
import { resolve } from 'node:path'

const chatSource = readFileSync(resolve(process.cwd(), 'src/views/Chat.vue'), 'utf8')

describe('Chat auto-scroll streaming & user intent tracking', () => {
  it('binds scroll, wheel, and touch events on the messages container', () => {
    // 验证 .messages 容器绑定了 handleScroll, handleWheel, handleTouchStart, handleTouchMove
    expect(chatSource).toMatch(/@scroll\.passive="handleScroll"/)
    expect(chatSource).toMatch(/@wheel\.passive="handleWheel"/)
    expect(chatSource).toMatch(/@touchstart\.passive="handleTouchStart"/)
    expect(chatSource).toMatch(/@touchmove\.passive="handleTouchMove"/)
  })

  it('observes latest message DOM resize with onLastMessageRef', () => {
    // 验证最后一条消息行绑定了动态 ref 回调 onLastMessageRef
    expect(chatSource).toMatch(/onLastMessageRef\(el as HTMLElement \| null\)/)
    expect(chatSource).toMatch(/setupLastMsgObserver/)
  })

  it('tracks all streaming dynamic fields beyond content (thinking, tool calls, workflow)', () => {
    // 验证流式追踪全面覆盖思考过程、工具调用、工作流及正文
    expect(chatSource).toMatch(/lastMsgStreamTrack/)
    expect(chatSource).toMatch(/thinking_content/)
    expect(chatSource).toMatch(/tool_calls/)
    expect(chatSource).toMatch(/workflowData/)
  })

  it('provides programmatic scroll guards and instant scrolling during streaming', () => {
    // 验证避免流式平滑动画冲突（默认 smooth = false）与程序化滚动状态保护
    expect(chatSource).toMatch(/function scrollToBottom\(smooth = false\)/)
    expect(chatSource).toMatch(/isProgrammaticScroll = true/)
  })

  it('shows dynamic floating scroll-to-bottom badge when streaming while user scrolled away', () => {
    // 验证用户向上翻阅且有新流式输出时呈现呼吸灯提示
    expect(chatSource).toMatch(/has-new-stream/)
    expect(chatSource).toMatch(/btn-streaming-dot/)
    expect(chatSource).toMatch(/有新内容 · 回到底部/)
  })

  it('ensures active streaming row has content-visibility visible for precise scroll calculation', () => {
    // 验证流式活跃行不被 content-visibility 截断高度计算
    expect(chatSource).toMatch(/\.message-row\.streaming-active-row[\s\S]*?content-visibility:\s*visible/s)
  })
})
