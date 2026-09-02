/**
 * @file Mermaid 图表渲染与深色科技主题配置工具模块
 * @description 集成 Mermaid.js 渲染引擎，提供赛博深色主题样式注入、中文排版字体兼容、DOMPurify XSS 安全过滤、
 * 离线沙箱隔离渲染队列、常见语法容错自动修复及高分辨率 SVG/PNG 图片导出下载功能
 */

// frontend/src/utils/mermaid.ts
import DOMPurify from 'dompurify'

/** Mermaid 实例 API 类型 */
export type MermaidApi = typeof import('mermaid')['default']
let mermaidInstance: MermaidApi | null = null
let initPromise: Promise<MermaidApi | null> | null = null

// 串行渲染任务队列，防止 Mermaid D3 DOM 操作并发竞争
let renderQueue: Promise<unknown> = Promise.resolve()

/**
 * 图表类型元数据描述信息
 */
export interface DiagramTypeInfo {
  /** 图表类型英文标识 (如 'flowchart', 'sequence', 'pie' 等) */
  type: string
  /** 图表中文可读显示标签 */
  label: string
  /** 图标名称 (Iconify 格式) */
  icon: string
}

/**
 * Mermaid 渲染自定配置选项
 */
export interface RenderOptions {
  /** 流程图连线弯曲形态 ('linear': 折线, 'basis': 贝塞尔平滑, 'step': 阶梯线 等) */
  curve?: 'linear' | 'basis' | 'cardinal' | 'monotoneY' | 'step'
  /** 节点之间的水平间距 (像素) */
  nodeSpacing?: number
  /** 层级之间的垂直间距 (像素) */
  rankSpacing?: number
}


/**
 * Standard Chinese & CJK font family stack with dark tech primary fallbacks.
 */
const CJK_FONT_FAMILY =
  "'Inter', -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'PingFang SC', 'Hiragino Sans GB', 'Microsoft YaHei', 'WenQuanYi Micro Hei', 'Noto Sans CJK SC', 'Source Han Sans CN', sans-serif"

/**
 * Cyber/Dark Tech theme variables matching Minefolio's design system.
 */
const MINELOFIO_THEME_VARIABLES = {
  darkMode: true,
  background: 'transparent',
  mainBkg: '#0b172a',
  nodeBorder: '#00d4ff',
  textColor: '#f1f5f9',
  lineColor: '#38bdf8',
  fontSize: '13px',
  fontFamily: CJK_FONT_FAMILY,

  // Flowchart
  nodeTextColor: '#f1f5f9',
  primaryColor: '#0d2238',
  primaryTextColor: '#f1f5f9',
  primaryBorderColor: '#00d4ff',
  edgeLabelBackground: '#0b1329',
  clusterBkg: 'rgba(12, 20, 38, 0.75)',
  clusterBorder: 'rgba(0, 212, 255, 0.4)',
  defaultLinkColor: '#38bdf8',
  titleColor: '#00d4ff',

  // Sequence
  actorBkg: '#0d2238',
  actorBorder: '#00d4ff',
  actorTextColor: '#f1f5f9',
  actorLineColor: '#38bdf8',
  signalColor: '#38bdf8',
  signalTextColor: '#f1f5f9',
  labelBoxBkgColor: '#0d2238',
  labelBoxBorderColor: '#00d4ff',
  labelTextColor: '#f1f5f9',
  loopTextColor: '#f1f5f9',
  noteBorderColor: '#8b5cf6',
  noteBkgColor: 'rgba(124, 58, 237, 0.2)',
  noteTextColor: '#e2e8f0',
  activationBorderColor: '#00d4ff',
  activationBkgColor: 'rgba(0, 212, 255, 0.25)',

  // Pie
  pie1: '#00d4ff',
  pie2: '#7c3aed',
  pie3: '#10b981',
  pie4: '#f59e0b',
  pie5: '#ec4899',
  pie6: '#06b6d4',
  pie7: '#8b5cf6',
  pie8: '#3b82f6',
  pie9: '#14b8a6',
  pie10: '#f97316',
  pie11: '#6366f1',
  pie12: '#84cc16',
  pieTitleTextSize: '16px',
  pieTitleTextColor: '#00d4ff',
  pieSectionTextColor: '#ffffff',
  pieLegendTextColor: '#e2e8f0',
  pieStrokeColor: '#0b172a',
  pieStrokeWidth: '2px',

  // State / Class
  classText: '#f1f5f9',
  stateBkg: '#0d2238',
  stateLabelColor: '#f1f5f9',
  altBackground: 'rgba(15, 23, 42, 0.85)',

  // Mindmap
  mindmapTextColor: '#f1f5f9',

  // Git
  git0: '#00d4ff',
  git1: '#7c3aed',
  git2: '#10b981',
  git3: '#f59e0b',
  gitBranchLabel0: '#00d4ff',
  gitBranchLabel1: '#7c3aed',
  gitBranchLabel2: '#10b981',
  gitBranchLabel3: '#f59e0b',

  // Quadrant
  quadrant1Fill: 'rgba(0, 212, 255, 0.12)',
  quadrant2Fill: 'rgba(124, 58, 237, 0.12)',
  quadrant3Fill: 'rgba(245, 158, 11, 0.12)',
  quadrant4Fill: 'rgba(16, 185, 129, 0.12)',
  quadrant1TextFill: '#00d4ff',
  quadrant2TextFill: '#a78bfa',
  quadrant3TextFill: '#fbbf24',
  quadrant4TextFill: '#34d399',
  quadrantPointFill: '#00d4ff',
  quadrantPointTextFill: '#f1f5f9',
  quadrantXAxisTextFill: '#94a3b8',
  quadrantYAxisTextFill: '#94a3b8',
}

const THEME_CUSTOM_CSS = `
  /* ── High-Tech Node Cards & Shapes ── */
  .node rect, .node circle, .node ellipse, .node polygon, .node path {
    fill: #0c1a2e !important;
    stroke: #00d4ff !important;
    stroke-width: 1.5px !important;
    rx: 8px !important;
    ry: 8px !important;
    filter: drop-shadow(0 2px 8px rgba(0, 212, 255, 0.12));
    transition: stroke 0.2s ease, filter 0.2s ease;
  }
  .node:hover rect, .node:hover circle, .node:hover ellipse, .node:hover polygon {
    stroke: #38bdf8 !important;
    stroke-width: 2px !important;
    filter: drop-shadow(0 0 10px rgba(0, 212, 255, 0.35));
  }

  /* ── Subgraphs & Clusters (Group Containers) ── */
  .cluster rect {
    fill: rgba(10, 18, 36, 0.65) !important;
    stroke: rgba(0, 212, 255, 0.35) !important;
    stroke-width: 1.5px !important;
    stroke-dasharray: 6 4 !important;
    rx: 12px !important;
    ry: 12px !important;
  }
  .cluster-label text, .cluster text, .cluster-label span, .cluster span, .cluster .nodeLabel {
    font-family: ${CJK_FONT_FAMILY} !important;
    fill: #00d4ff !important;
    color: #00d4ff !important;
    font-weight: 700 !important;
    font-size: 13px !important;
    letter-spacing: 0.5px !important;
  }

  /* ── Flowchart Edges & Connector Lines (Straight Lines with Rounded Joints) ── */
  .edgePath .path, .edge-thickness-normal, .flowchart-link {
    stroke: #38bdf8 !important;
    stroke-width: 1.8px !important;
    stroke-linecap: round !important;
    stroke-linejoin: round !important;
    opacity: 0.92;
  }
  .edgePath:hover .path {
    stroke: #00d4ff !important;
    stroke-width: 2.2px !important;
    filter: drop-shadow(0 0 6px rgba(0, 212, 255, 0.6));
  }
  .marker, marker path, #flowchart-pointEnd, #statediagram-barbEnd, [id^="flowchart-"] {
    fill: #38bdf8 !important;
    stroke: #38bdf8 !important;
  }

  /* ── Edge Labels & Pills ── */
  .edgeLabel {
    background-color: rgba(11, 19, 41, 0.95) !important;
    border: 1px solid rgba(0, 212, 255, 0.3) !important;
    border-radius: 4px !important;
    padding: 2px 6px !important;
    color: #bae6fd !important;
    font-size: 11px !important;
    font-weight: 500 !important;
    box-shadow: 0 2px 6px rgba(0, 0, 0, 0.4);
  }
  .edgeLabel rect {
    fill: rgba(11, 19, 41, 0.95) !important;
    stroke: rgba(0, 212, 255, 0.3) !important;
    rx: 4px !important;
    ry: 4px !important;
  }
  .edgeLabel span, .edgeLabel p {
    font-family: ${CJK_FONT_FAMILY} !important;
    color: #bae6fd !important;
    fill: #bae6fd !important;
  }

  /* ── Node Labels & Text Typography ── */
  .node text, .node span, .nodeLabel, .label, .label text, text.actor, .actor > tspan, .labelText, .node .label {
    font-family: ${CJK_FONT_FAMILY} !important;
    fill: #f1f5f9 !important;
    color: #f1f5f9 !important;
    font-size: 12.5px !important;
    font-weight: 500 !important;
  }
  foreignObject, foreignObject div, foreignObject span, foreignObject p {
    font-family: ${CJK_FONT_FAMILY} !important;
    color: #f1f5f9 !important;
    text-align: center;
    word-break: break-word;
    line-height: 1.4 !important;
  }

  /* ── Sequence Diagrams ── */
  .actor {
    fill: #0c1a2e !important;
    stroke: #00d4ff !important;
    stroke-width: 1.5px !important;
    rx: 8px !important;
    ry: 8px !important;
  }
  .actor-line {
    stroke: rgba(56, 189, 248, 0.4) !important;
    stroke-dasharray: 4 4 !important;
    stroke-width: 1.5px !important;
  }
  .messageLine0, .messageLine1 {
    stroke: #38bdf8 !important;
    stroke-width: 1.8px !important;
    stroke-linecap: round !important;
    stroke-linejoin: round !important;
  }
  .messageText {
    fill: #e2e8f0 !important;
    font-size: 12px !important;
  }
  .note {
    fill: rgba(124, 58, 237, 0.2) !important;
    stroke: #8b5cf6 !important;
    stroke-width: 1.2px !important;
    rx: 6px !important;
    ry: 6px !important;
  }
  .noteText {
    fill: #ddd6fe !important;
    font-size: 11.5px !important;
  }

  /* ── Mindmap ── */
  .mindmap-node rect, .mindmap-node circle, .mindmap-node path {
    rx: 8px !important;
    ry: 8px !important;
  }

  /* ── Pie & Titles ── */
  .pieTitleText {
    font-family: ${CJK_FONT_FAMILY} !important;
    fill: #00d4ff !important;
    font-weight: 700 !important;
    font-size: 16px !important;
  }
  .legend text {
    font-family: ${CJK_FONT_FAMILY} !important;
    fill: #cbd5e1 !important;
    font-size: 12px !important;
  }
`

/**
 * 异步初始化并获取 Mermaid API 单例实例
 * @description 配置全局深色科技主题变量、CJK 中文字体栈、防止错误直接输出到 DOM 的安全配置以及解析错误静默拦截
 * @returns Mermaid API 单例对象或 null (若加载失败)
 */
export async function ensureMermaid(): Promise<MermaidApi | null> {
  if (mermaidInstance) return mermaidInstance
  if (initPromise) return initPromise

  initPromise = (async () => {
    try {
      const mod = await import('mermaid')
      const api = mod.default
      api.initialize({
        startOnLoad: false,
        suppressErrorRendering: true, // 严禁 Mermaid 直接将错误 SVG 写入 DOM
        securityLevel: 'loose',
        theme: 'base',
        themeVariables: MINELOFIO_THEME_VARIABLES,
        themeCSS: THEME_CUSTOM_CSS,
        fontFamily: CJK_FONT_FAMILY,
        flowchart: {
          htmlLabels: true,
          useMaxWidth: true,
          curve: 'linear', // 默认使用带圆角拐角的折线连线
          nodeSpacing: 50,
          rankSpacing: 60,
          padding: 18,
          defaultRenderer: 'dagre-wrapper',
        },
        sequence: {
          useMaxWidth: true,
          showSequenceNumbers: false,
        },
        pie: {
          useMaxWidth: true,
        },
        mindmap: {
          useMaxWidth: true,
        },
      })

      // 屏蔽默认的 parseError 处理器
      if (typeof api.setParseErrorHandler === 'function') {
        api.setParseErrorHandler(() => {})
      }
      api.parseError = () => {}

      mermaidInstance = api
      return api
    } catch (err) {
      console.warn('Failed to load or initialize Mermaid:', err)
      return null
    }
  })()

  return initPromise
}

/**
 * 根据 Mermaid 代码首行智能识别推断图表类型
 * @param code Mermaid 代码文本
 * @returns 图表类型元数据对象 (包含类型标识、中文标签与图标)
 */
export function detectDiagramType(code: string): DiagramTypeInfo {
  const trimmed = code.trim().replace(/^%%[^\n]*\n+/gm, '') // 去除首部注释
  const firstLine = trimmed.split('\n')[0]?.trim().toLowerCase() || ''

  if (firstLine.startsWith('graph') || firstLine.startsWith('flowchart')) {
    return { type: 'flowchart', label: '流程图', icon: 'ph:git-fork' }
  }
  if (firstLine.startsWith('sequencediagram')) {
    return { type: 'sequence', label: '时序图', icon: 'ph:arrows-left-right' }
  }
  if (firstLine.startsWith('classdiagram')) {
    return { type: 'class', label: '类图', icon: 'ph:tree-structure' }
  }
  if (firstLine.startsWith('statediagram')) {
    return { type: 'state', label: '状态图', icon: 'ph:git-commit' }
  }
  if (firstLine.startsWith('erdiagram')) {
    return { type: 'er', label: '实体关系图', icon: 'ph:database' }
  }
  if (firstLine.startsWith('journey')) {
    return { type: 'journey', label: '用户旅程图', icon: 'ph:path' }
  }
  if (firstLine.startsWith('gantt')) {
    return { type: 'gantt', label: '甘特图', icon: 'ph:chart-bar-horizontal' }
  }
  if (firstLine.startsWith('pie')) {
    return { type: 'pie', label: '资产饼图', icon: 'ph:chart-pie' }
  }
  if (firstLine.startsWith('quadrantchart')) {
    return { type: 'quadrant', label: '象限图', icon: 'ph:grid-four' }
  }
  if (firstLine.startsWith('requirementdiagram')) {
    return { type: 'requirement', label: '需求图', icon: 'ph:clipboard-text' }
  }
  if (firstLine.startsWith('gitgraph')) {
    return { type: 'git', label: 'Git分支图', icon: 'ph:git-branch' }
  }
  if (firstLine.startsWith('c4context') || firstLine.startsWith('c4container') || firstLine.startsWith('c4component')) {
    return { type: 'c4', label: 'C4架构图', icon: 'ph:squares-four' }
  }
  if (firstLine.startsWith('mindmap')) {
    return { type: 'mindmap', label: '思维导图', icon: 'ph:graph' }
  }
  if (firstLine.startsWith('timeline')) {
    return { type: 'timeline', label: '时间线', icon: 'ph:clock-countdown' }
  }
  if (firstLine.startsWith('sankey')) {
    return { type: 'sankey', label: '资金流向桑基图', icon: 'ph:flow-arrow' }
  }
  if (firstLine.startsWith('xychart')) {
    return { type: 'xychart', label: '趋势坐标图', icon: 'ph:chart-line' }
  }
  if (firstLine.startsWith('block')) {
    return { type: 'block', label: '块状结构图', icon: 'ph:bounding-box' }
  }

  return { type: 'diagram', label: 'Mermaid 图表', icon: 'ph:projector-screen-chart' }
}

/**
 * 针对 LLM 大模型生成的 Mermaid 代码进行智能语法修复与净化
 * @description 剔除包裹的代码块 Markdown 标记 (```mermaid)，自动修复中文节点标签未加双引号导致的语法解析错误 (如 A[应急金 (3-6月)] -> A["应急金 (3-6月)"])
 * @param raw 原始 Mermaid 代码
 * @returns 修复净化后的 Mermaid 代码字符串
 */
export function sanitizeMermaid(raw: string): string {
  if (!raw) return ''
  let text = raw.trim()

  // 去除包裹的代码块标记
  if (text.startsWith('```mermaid')) {
    text = text.replace(/^```mermaid\s*\n?/, '').replace(/\n?```$/, '')
  } else if (text.startsWith('```')) {
    text = text.replace(/^```[a-z]*\s*\n?/, '').replace(/\n?```$/, '')
  }

  // 去除尾部多余的代码符号
  text = text.replace(/\s*```\s*$/, '').trim()

  // 自动修复大模型常见的流程图括号未加引号错误
  const lines = text.split('\n')
  const repairedLines = lines.map((line) => {
    return line.replace(/([\w\u4e00-\u9fa5]+)\s*\[([^"\]]+)\]/gu, (match, id, label) => {
      const trimmedLabel = label.trim()
      if (/[()/:,，、%]/.test(trimmedLabel) && !trimmedLabel.startsWith('"') && !trimmedLabel.endsWith('"')) {
        const escaped = trimmedLabel.replace(/"/g, "'")
        return `${id}["${escaped}"]`
      }
      return match
    })
  })

  return repairedLines.join('\n').trim()
}

/**
 * 净化 Mermaid 渲染出的 SVG 输出 (保留 foreignObject 与中文字符排版 HTML 标签，过滤危险脚本与事件属性)
 * @param rawSvg Mermaid 原始生成的 SVG 字符串
 * @returns 净化后的安全 SVG 字符串
 */
export function sanitizeMermaidSvg(rawSvg: string): string {
  return DOMPurify.sanitize(rawSvg, {
    ADD_TAGS: ['foreignobject', 'foreignObject', 'switch'],
    ADD_ATTR: [
      'dominant-baseline',
      'text-anchor',
      'transform-origin',
      'edge-label',
      'alignment-baseline',
      'xmlns',
      'xmlns:xlink',
      'xlink:href',
    ],
    HTML_INTEGRATION_POINTS: { foreignobject: true, foreignObject: true },
    FORBID_TAGS: ['script', 'iframe', 'object', 'embed'],
    FORBID_ATTR: ['onerror', 'onload', 'onclick', 'onmouseover'],
  })
}

/**
 * 在离线隐藏沙箱容器中执行单次 Mermaid 渲染并自动容错重试
 * @param id 渲染图表 DOM ID
 * @param code Mermaid 代码
 * @param options 自定义渲染参数
 * @returns 包含净化后 SVG 字符串的对象
 */
async function executeRender(id: string, code: string, options?: RenderOptions): Promise<{ svg: string }> {
  const mermaid = await ensureMermaid()
  if (!mermaid) {
    throw new Error('Mermaid renderer is not available')
  }

  // 若指定了连线曲率等选项则动态覆盖配置
  const curveType = options?.curve || 'linear'
  mermaid.initialize({
    flowchart: {
      htmlLabels: true,
      useMaxWidth: true,
      curve: curveType,
      nodeSpacing: options?.nodeSpacing || 50,
      rankSpacing: options?.rankSpacing || 60,
      padding: 18,
      defaultRenderer: 'dagre-wrapper',
    },
  })

  // 创建屏幕外的隔离沙箱容器，避免 Mermaid 直接操作当前可视 DOM 引起画面闪烁与冲突
  let sandbox: HTMLElement | null = null
  if (typeof document !== 'undefined') {
    sandbox = document.createElement('div')
    sandbox.id = `mermaid-sandbox-${id}`
    sandbox.style.position = 'fixed'
    sandbox.style.left = '-99999px'
    sandbox.style.top = '-99999px'
    sandbox.style.visibility = 'hidden'
    sandbox.style.opacity = '0'
    sandbox.style.pointerEvents = 'none'
    document.body.appendChild(sandbox)
  }

  try {
    // 1. 尝试直接渲染原始代码
    try {
      const { svg } = await mermaid.render(id, code, sandbox || undefined)
      const sanitized = sanitizeMermaidSvg(svg)
      return { svg: sanitized }
    } catch (initialErr) {
      // 2. 若失败，尝试自动修复语法错误并重试渲染
      const sanitizedCode = sanitizeMermaid(code)
      if (sanitizedCode && sanitizedCode !== code) {
        try {
          if (sandbox) sandbox.innerHTML = ''
          const retryId = `${id}-retry`
          const { svg } = await mermaid.render(retryId, sanitizedCode, sandbox || undefined)
          const sanitized = sanitizeMermaidSvg(svg)
          return { svg: sanitized }
        } catch {
          throw initialErr
        }
      }
      throw initialErr
    }
  } finally {
    if (sandbox && sandbox.parentElement) {
      sandbox.remove()
    }
  }
}

/**
 * 通过串行渲染队列将 Mermaid 代码安全转换为净化后的 SVG 字符串
 * @param id 图表唯一标识
 * @param code Mermaid 图表代码
 * @param options 可选的布局与线条连线形态配置
 * @returns 包含生成 SVG 字符串的 Promise
 */
export function renderMermaidSvg(id: string, code: string, options?: RenderOptions): Promise<{ svg: string }> {
  const nextTask = renderQueue.catch(() => {}).then(() => executeRender(id, code, options))
  renderQueue = nextTask
  return nextTask
}

/**
 * 触发浏览器将 SVG 内容下载为 .svg 矢量图文件
 * @param svgContent SVG 文本内容
 * @param filename 导出的文件名 (默认 'minefolio-diagram.svg')
 */
export function downloadSvg(svgContent: string, filename = 'minefolio-diagram.svg') {
  const blob = new Blob([svgContent], { type: 'image/svg+xml;charset=utf-8' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = filename
  document.body.appendChild(a)
  a.click()
  document.body.removeChild(a)
  URL.revokeObjectURL(url)
}

/**
 * 将渲染出的 SVG 转换为高清晰度 PNG 图片并触发浏览器下载
 * @param svgContent SVG 文本内容
 * @param filename 导出的文件名 (默认 'minefolio-diagram.png')
 * @param scale 放大缩放倍率 (默认 2 倍高清导出)
 * @returns Promise<void>
 */
export function downloadPng(svgContent: string, filename = 'minefolio-diagram.png', scale = 2): Promise<void> {
  return new Promise((resolve, reject) => {
    try {
      const parser = new DOMParser()
      const doc = parser.parseFromString(svgContent, 'image/svg+xml')
      const svgEl = doc.querySelector('svg')
      if (!svgEl) {
        throw new Error('Invalid SVG content')
      }

      // 计算真实尺寸
      let width = parseFloat(svgEl.getAttribute('width') || '0')
      let height = parseFloat(svgEl.getAttribute('height') || '0')

      if (!width || !height) {
        const viewBox = svgEl.getAttribute('viewBox')
        if (viewBox) {
          const parts = viewBox.split(/\s+/).map(Number)
          if (parts.length === 4 && parts[2] && parts[3]) {
            width = parts[2]
            height = parts[3]
          }
        }
      }

      if (!width || !height) {
        width = 800
        height = 600
      }

      const canvas = document.createElement('canvas')
      canvas.width = width * scale
      canvas.height = height * scale
      const ctx = canvas.getContext('2d')
      if (!ctx) {
        throw new Error('Canvas context not available')
      }

      // 绘制深色背景以契合 Minefolio 科技主题
      ctx.fillStyle = '#0b172a'
      ctx.fillRect(0, 0, canvas.width, canvas.height)
      ctx.scale(scale, scale)

      const blob = new Blob([svgContent], { type: 'image/svg+xml;charset=utf-8' })
      const url = URL.createObjectURL(blob)
      const img = new Image()

      img.onload = () => {
        ctx.drawImage(img, 0, 0, width, height)
        URL.revokeObjectURL(url)

        canvas.toBlob((pngBlob) => {
          if (!pngBlob) {
            reject(new Error('Failed to generate PNG blob'))
            return
          }
          const pngUrl = URL.createObjectURL(pngBlob)
          const a = document.createElement('a')
          a.href = pngUrl
          a.download = filename
          document.body.appendChild(a)
          a.click()
          document.body.removeChild(a)
          URL.revokeObjectURL(pngUrl)
          resolve()
        }, 'image/png')
      }

      img.onerror = (e) => {
        URL.revokeObjectURL(url)
        reject(e)
      }

      img.src = url
    } catch (err) {
      reject(err)
    }
  })
}

