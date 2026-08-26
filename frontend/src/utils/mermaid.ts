// frontend/src/utils/mermaid.ts
import DOMPurify from 'dompurify'

export type MermaidApi = typeof import('mermaid')['default']
let mermaidInstance: MermaidApi | null = null
let initPromise: Promise<MermaidApi | null> | null = null

export interface DiagramTypeInfo {
  type: string
  label: string
  icon: string
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
  mainBkg: '#0f172a',
  nodeBorder: '#00d4ff',
  textColor: '#e2e8f0',
  lineColor: '#38bdf8',
  fontSize: '13px',
  fontFamily: CJK_FONT_FAMILY,

  // Flowchart
  nodeTextColor: '#e2e8f0',
  primaryColor: '#0f2438',
  primaryTextColor: '#e2e8f0',
  primaryBorderColor: '#00d4ff',
  edgeLabelBackground: '#0b1329',
  clusterBkg: 'rgba(15, 23, 42, 0.85)',
  clusterBorder: 'rgba(0, 212, 255, 0.35)',
  defaultLinkColor: '#38bdf8',
  titleColor: '#00d4ff',

  // Sequence
  actorBkg: '#0f2438',
  actorBorder: '#00d4ff',
  actorTextColor: '#e2e8f0',
  actorLineColor: '#38bdf8',
  signalColor: '#38bdf8',
  signalTextColor: '#e2e8f0',
  labelBoxBkgColor: '#0f2438',
  labelBoxBorderColor: '#00d4ff',
  labelTextColor: '#e2e8f0',
  loopTextColor: '#e2e8f0',
  noteBorderColor: '#7c3aed',
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
  pieTitleTextSize: '15px',
  pieTitleTextColor: '#00d4ff',
  pieSectionTextColor: '#ffffff',
  pieLegendTextColor: '#e2e8f0',
  pieStrokeColor: '#0f172a',
  pieStrokeWidth: '2px',

  // State / Class
  classText: '#e2e8f0',
  stateBkg: '#0f2438',
  stateLabelColor: '#e2e8f0',
  altBackground: 'rgba(15, 23, 42, 0.8)',

  // Mindmap
  mindmapTextColor: '#e2e8f0',

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
  quadrantPointTextFill: '#e2e8f0',
  quadrantXAxisTextFill: '#94a3b8',
  quadrantYAxisTextFill: '#94a3b8',
}

const THEME_CUSTOM_CSS = `
  /* High contrast Chinese & Unicode label styling across all diagrams */
  .node text, .node span, .nodeLabel, .label, .label text, text.actor, .actor > tspan, .labelText, .node .label {
    font-family: ${CJK_FONT_FAMILY} !important;
    fill: #e2e8f0 !important;
    color: #e2e8f0 !important;
  }
  foreignObject, foreignObject div, foreignObject span, foreignObject p {
    font-family: ${CJK_FONT_FAMILY} !important;
    color: #e2e8f0 !important;
    text-align: center;
    word-break: break-word;
  }
  .cluster-label text, .cluster text, .cluster-label span, .cluster span, .cluster .nodeLabel {
    font-family: ${CJK_FONT_FAMILY} !important;
    fill: #00d4ff !important;
    color: #00d4ff !important;
    font-weight: 600 !important;
  }
  .edgeLabel, .edgeLabel span, .edgeLabel p {
    font-family: ${CJK_FONT_FAMILY} !important;
    color: #38bdf8 !important;
    fill: #38bdf8 !important;
    background-color: #0b1329 !important;
  }
  .edgeLabel rect {
    fill: #0b1329 !important;
    opacity: 0.9;
  }
  .pieTitleText {
    font-family: ${CJK_FONT_FAMILY} !important;
    fill: #00d4ff !important;
    font-weight: 600 !important;
  }
  .legend text {
    font-family: ${CJK_FONT_FAMILY} !important;
    fill: #e2e8f0 !important;
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
        securityLevel: 'loose',
        theme: 'base',
        themeVariables: MINELOFIO_THEME_VARIABLES,
        themeCSS: THEME_CUSTOM_CSS,
        fontFamily: CJK_FONT_FAMILY,
        flowchart: {
          htmlLabels: true,
          useMaxWidth: true,
          curve: 'basis',
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
 * Renders a Mermaid diagram string to a sanitized SVG string.
 */
export async function renderMermaidSvg(id: string, code: string): Promise<{ svg: string }> {
  const mermaid = await ensureMermaid()
  if (!mermaid) {
    throw new Error('Mermaid renderer is not available')
  }

  // 1. Try rendering the original code
  try {
    const { svg } = await mermaid.render(id, code)
    const sanitized = sanitizeMermaidSvg(svg)
    return { svg: sanitized }
  } catch (initialErr) {
    // 2. If it failed, attempt auto-repair and retry
    const sanitizedCode = sanitizeMermaid(code)
    if (sanitizedCode && sanitizedCode !== code) {
      try {
        const retryId = `${id}-retry`
        const { svg } = await mermaid.render(retryId, sanitizedCode)
        const sanitized = sanitizeMermaidSvg(svg)
        return { svg: sanitized }
      } catch {
        throw initialErr
      }
    }
    throw initialErr
  }
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
      ctx.fillStyle = '#0f172a'
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
