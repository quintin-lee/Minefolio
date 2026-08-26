// frontend/src/utils/mermaid.ts
import DOMPurify from 'dompurify'

export type MermaidApi = typeof import('mermaid')['default']
let mermaidInstance: MermaidApi | null = null
let initPromise: Promise<MermaidApi | null> | null = null

// Serial rendering queue to prevent concurrency race conditions in Mermaid D3 DOM operations
let renderQueue: Promise<unknown> = Promise.resolve()

export interface DiagramTypeInfo {
  type: string
  label: string
  icon: string
}

export interface RenderOptions {
  curve?: 'linear' | 'basis' | 'cardinal' | 'monotoneY' | 'step'
  nodeSpacing?: number
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
 * Initializes and returns the singleton Mermaid API instance.
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
        suppressErrorRendering: true, // Strictly prohibit Mermaid from writing error SVGs to DOM
        securityLevel: 'loose',
        theme: 'base',
        themeVariables: MINELOFIO_THEME_VARIABLES,
        themeCSS: THEME_CUSTOM_CSS,
        fontFamily: CJK_FONT_FAMILY,
        flowchart: {
          htmlLabels: true,
          useMaxWidth: true,
          curve: 'linear', // Straight lines with rounded joints/corners by default
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

      // Suppress default parseError handler
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
 * Auto-detects diagram type from Mermaid source code.
 */
export function detectDiagramType(code: string): DiagramTypeInfo {
  const trimmed = code.trim().replace(/^%%[^\n]*\n+/gm, '') // strip comments
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
 * Intelligent sanitization and repair for LLM-generated Mermaid code.
 */
export function sanitizeMermaid(raw: string): string {
  if (!raw) return ''
  let text = raw.trim()

  // Remove wrapping markdown code fences if present
  if (text.startsWith('```mermaid')) {
    text = text.replace(/^```mermaid\s*\n?/, '').replace(/\n?```$/, '')
  } else if (text.startsWith('```')) {
    text = text.replace(/^```[a-z]*\s*\n?/, '').replace(/\n?```$/, '')
  }

  // Remove trailing markdown code ticks
  text = text.replace(/\s*```\s*$/, '').trim()

  // Auto-repair common LLM flowchart syntax issues:
  // Node labels with unquoted parentheses/brackets (e.g. A[应急储备金 (3-6个月)] -> A["应急储备金 (3-6个月)"])
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
 * Sanitizes Mermaid SVG output preserving <foreignObject> and HTML labels for Chinese text.
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
 * Executes a single render pass inside an isolated offscreen sandbox container.
 */
async function executeRender(id: string, code: string, options?: RenderOptions): Promise<{ svg: string }> {
  const mermaid = await ensureMermaid()
  if (!mermaid) {
    throw new Error('Mermaid renderer is not available')
  }

  // Re-configure active curve if specified
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

  // Create an offscreen sandbox container to completely isolate Mermaid DOM ops from document.body
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
    // 1. Try rendering original code
    try {
      const { svg } = await mermaid.render(id, code, sandbox || undefined)
      const sanitized = sanitizeMermaidSvg(svg)
      return { svg: sanitized }
    } catch (initialErr) {
      // 2. If it failed, attempt auto-repair and retry
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
 * Renders a Mermaid diagram string to a sanitized SVG string via a sequential queue.
 */
export function renderMermaidSvg(id: string, code: string, options?: RenderOptions): Promise<{ svg: string }> {
  const nextTask = renderQueue.catch(() => {}).then(() => executeRender(id, code, options))
  renderQueue = nextTask
  return nextTask
}

/**
 * Triggers browser download for SVG content.
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
 * Exports rendered SVG to a high-DPI PNG image and triggers download.
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

      // Determine dimensions
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

      // Draw dark background matching Minefolio theme
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
