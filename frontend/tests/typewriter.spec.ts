import { describe, it, expect, vi } from 'vitest'
import { SmoothStreamWriter, type TypewriterTarget } from '@/utils/typewriter'

describe('SmoothStreamWriter', () => {
  function createFakeRaf() {
    let currentId = 0
    const callbacks = new Map<number, () => void>()

    const raf = (cb: () => void) => {
      const id = ++currentId
      callbacks.set(id, cb)
      return id
    }

    const caf = (id: number) => {
      callbacks.delete(id)
    }

    const step = () => {
      const entries = Array.from(callbacks.entries())
      callbacks.clear()
      for (const [, cb] of entries) {
        cb()
      }
      return entries.length
    }

    const runAll = (maxSteps = 1000) => {
      let count = 0
      while (callbacks.size > 0 && count < maxSteps) {
        step()
        count++
      }
      return count
    }

    return { raf, caf, step, runAll, getPendingCount: () => callbacks.size }
  }

  it('outputs small text smoothly character-by-character without batch dumping', () => {
    const fake = createFakeRaf()
    const target: TypewriterTarget = { content: '' }
    const writer = new SmoothStreamWriter(target, { raf: fake.raf, caf: fake.caf })

    writer.push('你好呀世界')

    // Initial state
    expect(target.content).toBe('')
    expect(writer.bufferLength).toBe(5)

    // Tick 1
    fake.step()
    expect(target.content.length).toBeGreaterThanOrEqual(1)
    expect(target.content.length).toBeLessThanOrEqual(2) // Smoothly 1 char
    const after1 = target.content

    // Tick 2
    fake.step()
    expect(target.content.length).toBeGreaterThan(after1.length)
    expect(target.content.length - after1.length).toBeLessThanOrEqual(2)

    // Run remaining ticks
    fake.runAll()
    expect(target.content).toBe('你好呀世界')
    expect(writer.bufferLength).toBe(0)
  })

  it('adaptively paces large bursts without stalling or single-frame dumping', () => {
    const fake = createFakeRaf()
    const target: TypewriterTarget = { content: '' }
    const writer = new SmoothStreamWriter(target, { raf: fake.raf, caf: fake.caf })

    // Simulate a workflow execution burst of 500 characters
    const burst = 'A'.repeat(500)
    writer.push(burst)

    // First frame should not dump all 500 chars at once!
    fake.step()
    expect(target.content.length).toBeGreaterThan(0)
    expect(target.content.length).toBeLessThan(30) // Max burst step capped

    // Should drain steadily across multiple frames
    const stepCount = fake.runAll()
    expect(stepCount).toBeGreaterThan(15) // Takes multiple frames, completely smooth
    expect(target.content).toBe(burst)
    expect(writer.hasPending).toBe(false)
  })

  it('pauses slightly at punctuation marks for natural breathing rhythm', () => {
    const fake = createFakeRaf()
    const target: TypewriterTarget = { content: '' }
    const writer = new SmoothStreamWriter(target, { raf: fake.raf, caf: fake.caf })

    writer.push('好。世界')

    // Step 1: types '好'
    fake.step()
    expect(target.content).toBe('好')

    // Step 2: encounters '。', outputs '。'
    fake.step()
    expect(target.content).toBe('好。')

    // Step 3: punctuation micro-pause frame: content does not change
    fake.step()
    expect(target.content).toBe('好。')

    // Step 4: resumes typing '世'
    fake.step()
    expect(target.content.length).toBeGreaterThan(2)

    fake.runAll()
    expect(target.content).toBe('好。世界')
  })

  it('finish() drains buffer gracefully and resolves promise', async () => {
    const fake = createFakeRaf()
    const target: TypewriterTarget = { content: '' }
    const writer = new SmoothStreamWriter(target, { raf: fake.raf, caf: fake.caf })

    writer.push('这是即将完成的工作流报告内容')

    let finished = false
    const finishPromise = writer.finish().then(() => {
      finished = true
    })

    expect(finished).toBe(false)

    // Run until drained
    fake.runAll()
    await finishPromise

    expect(finished).toBe(true)
    expect(target.content).toBe('这是即将完成的工作流报告内容')
    expect(writer.hasPending).toBe(false)
  })

  it('flushNow() flushes all remaining buffer immediately when requested', () => {
    const fake = createFakeRaf()
    const target: TypewriterTarget = { content: '' }
    const writer = new SmoothStreamWriter(target, { raf: fake.raf, caf: fake.caf })

    writer.push('正在生成的长文本内容...')
    expect(target.content).toBe('')

    writer.flushNow()
    expect(target.content).toBe('正在生成的长文本内容...')
    expect(writer.hasPending).toBe(false)
    expect(fake.getPendingCount()).toBe(0)
  })
})
