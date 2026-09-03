export interface TypewriterTarget {
  content: string
}

export interface TypewriterOptions {
  /**
   * 自定义 requestAnimationFrame，在测试或特定环境下可注入
   */
  raf?: (cb: () => void) => number
  /**
   * 自定义 cancelAnimationFrame
   */
  caf?: (id: number) => void
}

/**
 * 平滑打字机流式渲染写入器
 * @description 将突发到达的 SSE / 流式字符存入内部缓冲区，利用 requestAnimationFrame (RAF) 以拟真呼吸感打字节奏吐出字符到响应式消息中。
 * 具备自适应阻尼控速 (Adaptive Pacing)，防止单帧输出过多导致画面成批跳跃，也防止积压过多时严重脱节。
 */
export class SmoothStreamWriter {
  /** 待吐出的字符积压缓冲区 */
  private buf = ''
  /** 动画帧请求 ID */
  private rafId: number | null = null
  /** 写入器是否处于激活运行状态 */
  private running = true
  /** 写入器是否已关闭 */
  private closed = false
  /** 是否已进入完成收尾阶段 (finish) */
  private isFinishing = false
  /** 标点微顿歇跳帧计数 */
  private pauseFrames = 0
  /** finish 回调队列 */
  private finishResolvers: (() => void)[] = []

  private readonly raf: (cb: () => void) => number
  private readonly caf: (id: number) => void

  constructor(
    private readonly target: TypewriterTarget,
    options?: TypewriterOptions,
  ) {
    this.raf = options?.raf || (typeof requestAnimationFrame === 'function' ? requestAnimationFrame : (cb) => setTimeout(cb, 16) as unknown as number)
    this.caf = options?.caf || (typeof cancelAnimationFrame === 'function' ? cancelAnimationFrame : (id) => clearTimeout(id as unknown as NodeJS.Timeout))
    this.schedule()
  }

  /**
   * 向缓冲区追加新接收到的增量文本
   * @param text 增量字符串
   */
  push(text: string) {
    if (this.closed || !text) return
    this.buf += text
    if (this.rafId === null && this.running) {
      this.schedule()
    }
  }

  /** 当前缓冲区剩余积压字符数 */
  get bufferLength(): number {
    return this.buf.length
  }

  /** 是否仍有字符未输出 */
  get hasPending(): boolean {
    return this.buf.length > 0
  }

  /** 调度下一帧动画 tick */
  private schedule() {
    if (!this.running || this.closed) return
    if (this.rafId !== null) return
    this.rafId = this.raf(() => this.tick())
  }

  /**
   * 根据当前缓冲区积压深度自适应计算吐字步长：
   * - 极小积压 (1~8 字符)：每次 1 个字符（细腻单字效果，约 60 字/秒，符合真人拟真节奏）
   * - 小积压 (9~25 字符)：每次 1~2 个字符
   * - 中等积压 (26~80 字符)：每次 2~3 个字符
   * - 较大积压 (81~250 字符)：动态按比例加速，每次 Math.ceil(len / 25) 字符 (约 3~10 字)
   * - 极端突发或工作流瞬间返回 (>250 字符)：按比例平滑消化，每次 Math.min(20, Math.ceil(len / 16))
   */
  private calculateStep(): number {
    const len = this.buf.length
    if (len === 0) return 0

    // 收尾阶段适度加速，使剩余字符在 200~400ms 内平滑输完，绝不突兀整批蹦出
    if (this.isFinishing) {
      if (len <= 4) return 1
      if (len <= 15) return 2
      if (len <= 50) return 3
      return Math.min(20, Math.max(4, Math.ceil(len / 10)))
    }

    if (len <= 8) return 1
    if (len <= 25) return Math.random() < 0.65 ? 1 : 2
    if (len <= 80) return Math.random() < 0.5 ? 2 : 3
    if (len <= 250) return Math.min(8, Math.max(3, Math.ceil(len / 25)))
    return Math.min(20, Math.max(6, Math.ceil(len / 16)))
  }

  /** 单帧渲染更新函数 */
  private tick() {
    this.rafId = null
    if (!this.running || this.closed) return

    // 处理标点符号的自然呼吸微顿歇（仅在非收尾、缓冲区无严重积压时生效）
    if (this.pauseFrames > 0) {
      this.pauseFrames--
      if (this.buf) this.schedule()
      return
    }

    if (!this.buf) {
      if (this.isFinishing) {
        this.resolveFinish()
      }
      return
    }

    const peek = this.buf[0]!
    const isPunct = '，。！？；：、\n.,!?;:'.includes(peek)

    let step: number
    if (isPunct && !this.isFinishing && this.buf.length < 50) {
      // 标点符号单字吐出，并在下一帧产生极轻微停顿 (1 帧 ≈ 16ms, 换行 2 帧 ≈ 32ms)
      step = 1
      this.pauseFrames = peek === '\n' ? 2 : 1
    } else {
      step = this.calculateStep()
    }

    if (step > this.buf.length) step = this.buf.length
    if (step <= 0) step = 1

    this.target.content += this.buf.slice(0, step)
    this.buf = this.buf.slice(step)

    if (this.buf) {
      this.schedule()
    } else if (this.isFinishing) {
      this.resolveFinish()
    }
  }

  private resolveFinish() {
    this.stop()
    const resolvers = [...this.finishResolvers]
    this.finishResolvers = []
    resolvers.forEach((r) => r())
  }

  /**
   * 等待并确保缓冲区内所有积压字符全部平滑输出完成
   */
  async finish(): Promise<void> {
    if (this.closed || !this.buf) {
      this.resolveFinish()
      return
    }
    this.isFinishing = true
    if (this.rafId === null && this.running) {
      this.schedule()
    }
    await new Promise<void>((resolve) => {
      this.finishResolvers.push(resolve)
    })
  }

  /**
   * 立即将缓冲区中所有未输出字符刷入目标消息（仅用于用户强制取消或异常中断）
   */
  flushNow() {
    if (this.buf) {
      this.target.content += this.buf
      this.buf = ''
    }
    this.resolveFinish()
  }

  /** 停止动画循环并释放资源 */
  private stop() {
    this.closed = true
    this.running = false
    if (this.rafId !== null) {
      this.caf(this.rafId)
      this.rafId = null
    }
  }
}
