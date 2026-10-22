/**
 * @file Mermaid 图表渲染与深色科技主题配置工具模块
 * @description 集成 Mermaid.js 渲染引擎，提供赛博深色主题样式注入、中文排版字体兼容、DOMPurify XSS 安全过滤、
 * 离线沙箱隔离渲染队列、常见语法容错自动修复及高分辨率 SVG/PNG 图片导出下载功能
 */

// frontend/src/utils/mermaid.ts
import DOMPurify from 'dompurify'
import mermaid from 'mermaid'

/** Mermaid 实例 API 类型 */
export type MermaidApi = typeof mermaid

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
  /** 图表渲染主题风格 ('dark' | 'light') */
  theme?: MermaidThemeName
}


/**
 * Standard Chinese & CJK font family stack with dark tech primary fallbacks.
 */
const CJK_FONT_FAMILY =
  "'Inter', -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'PingFang SC', 'Hiragino Sans GB', 'Microsoft YaHei', 'WenQuanYi Micro Hei', 'Noto Sans CJK SC', 'Source Han Sans CN', sans-serif"

export type MermaidThemeName = 'dark' | 'light'

/** Theme-agnostic brand palette shared by dark & light modes. */
const BRAND_PIE_COLORS = {
  pie1: '#3b82f6',
  pie2: '#8b5cf6',
  pie3: '#10b981',
  pie4: '#f59e0b',
  pie5: '#ec4899',
  pie6: '#06b6d4',
  pie7: '#6366f1',
  pie8: '#f97316',
  pie9: '#14b8a6',
  pie10: '#a855f7',
  pie11: '#ef4444',
  pie12: '#84cc16',
}

/**
 * Dark "cyber tech" theme variables. Text colors use the highest-contrast
 * slate tones so labels stay readable over dark node fills.
 */
const DARK_THEME_VARIABLES = {
  darkMode: true,
  background: 'transparent',
  mainBkg: '#0b172a',
  nodeBorder: '#38bdf8',
  textColor: '#f8fafc',
  lineColor: '#38bdf8',
  fontSize: '13px',
  fontFamily: CJK_FONT_FAMILY,

  // Flowchart
  nodeTextColor: '#f8fafc',
  primaryColor: '#0d2238',
  primaryTextColor: '#f8fafc',
  primaryBorderColor: '#38bdf8',
  edgeLabelBackground: '#0b1329',
  clusterBkg: 'rgba(12, 20, 38, 0.75)',
  clusterBorder: 'rgba(56, 189, 248, 0.4)',
  defaultLinkColor: '#38bdf8',
  titleColor: '#38bdf8',

  // Sequence
  actorBkg: '#0d2238',
  actorBorder: '#38bdf8',
  actorTextColor: '#f8fafc',
  actorLineColor: '#38bdf8',
  signalColor: '#38bdf8',
  signalTextColor: '#f8fafc',
  labelBoxBkgColor: '#0d2238',
  labelBoxBorderColor: '#38bdf8',
  labelTextColor: '#f8fafc',
  loopTextColor: '#f8fafc',
  noteBorderColor: '#8b5cf6',
  noteBkgColor: 'rgba(124, 58, 237, 0.2)',
  noteTextColor: '#e2e8f0',
  activationBorderColor: '#38bdf8',
  activationBkgColor: 'rgba(56, 189, 248, 0.25)',

  // Pie
  ...BRAND_PIE_COLORS,
  pieTitleTextSize: '16px',
  pieTitleTextColor: '#38bdf8',
  pieSectionTextColor: '#ffffff',
  pieLegendTextColor: '#f8fafc',
  pieStrokeColor: '#0b172a',
  pieStrokeWidth: '2px',

  // State / Class
  classText: '#f8fafc',
  stateBkg: '#0d2238',
  stateLabelColor: '#f8fafc',
  altBackground: 'rgba(15, 23, 42, 0.85)',

  // Mindmap
  mindmapTextColor: '#f8fafc',

  // Git
  git0: '#38bdf8',
  git1: '#8b5cf6',
  git2: '#10b981',
  git3: '#f59e0b',
  gitBranchLabel0: '#38bdf8',
  gitBranchLabel1: '#8b5cf6',
  gitBranchLabel2: '#10b981',
  gitBranchLabel3: '#f59e0b',

  // Quadrant
  quadrant1Fill: 'rgba(56, 189, 248, 0.12)',
  quadrant2Fill: 'rgba(139, 92, 246, 0.12)',
  quadrant3Fill: 'rgba(245, 158, 11, 0.12)',
  quadrant4Fill: 'rgba(16, 185, 129, 0.12)',
  quadrant1TextFill: '#38bdf8',
  quadrant2TextFill: '#a78bfa',
  quadrant3TextFill: '#fbbf24',
  quadrant4TextFill: '#34d399',
  quadrantPointFill: '#38bdf8',
  quadrantPointTextFill: '#f8fafc',
  quadrantXAxisTextFill: '#94a3b8',
  quadrantYAxisTextFill: '#94a3b8',
}

/**
 * Light theme variables. Dark slate text on white/soft surfaces.
 */
const LIGHT_THEME_VARIABLES = {
  darkMode: false,
  background: 'transparent',
  mainBkg: '#ffffff',
  nodeBorder: '#2563eb',
  textColor: '#0f172a',
  lineColor: '#2563eb',
  fontSize: '13px',
  fontFamily: CJK_FONT_FAMILY,

  // Flowchart
  nodeTextColor: '#0f172a',
  primaryColor: '#eff6ff',
  primaryTextColor: '#0f172a',
  primaryBorderColor: '#2563eb',
  edgeLabelBackground: '#ffffff',
  clusterBkg: 'rgba(241, 245, 249, 0.9)',
  clusterBorder: 'rgba(37, 99, 235, 0.35)',
  defaultLinkColor: '#2563eb',
  titleColor: '#1d4ed8',

  // Sequence
  actorBkg: '#eff6ff',
  actorBorder: '#2563eb',
  actorTextColor: '#0f172a',
  actorLineColor: '#2563eb',
  signalColor: '#2563eb',
  signalTextColor: '#0f172a',
  labelBoxBkgColor: '#eff6ff',
  labelBoxBorderColor: '#2563eb',
  labelTextColor: '#0f172a',
  loopTextColor: '#0f172a',
  noteBorderColor: '#7c3aed',
  noteBkgColor: 'rgba(124, 58, 237, 0.08)',
  noteTextColor: '#1e1b4b',
  activationBorderColor: '#2563eb',
  activationBkgColor: 'rgba(37, 99, 235, 0.12)',

  // Pie
  ...BRAND_PIE_COLORS,
  pieTitleTextSize: '16px',
  pieTitleTextColor: '#1d4ed8',
  pieSectionTextColor: '#ffffff',
  pieLegendTextColor: '#0f172a',
  pieStrokeColor: '#ffffff',
  pieStrokeWidth: '2px',

  // State / Class
  classText: '#0f172a',
  stateBkg: '#eff6ff',
  stateLabelColor: '#0f172a',
  altBackground: 'rgba(241, 245, 249, 0.9)',

  // Mindmap
  mindmapTextColor: '#0f172a',

  // Git
  git0: '#2563eb',
  git1: '#7c3aed',
  git2: '#059669',
  git3: '#d97706',
  gitBranchLabel0: '#2563eb',
  gitBranchLabel1: '#7c3aed',
  gitBranchLabel2: '#059669',
  gitBranchLabel3: '#d97706',

  // Quadrant
  quadrant1Fill: 'rgba(37, 99, 235, 0.08)',
  quadrant2Fill: 'rgba(124, 58, 237, 0.08)',
  quadrant3Fill: 'rgba(217, 119, 6, 0.08)',
  quadrant4Fill: 'rgba(5, 150, 105, 0.08)',
  quadrant1TextFill: '#1d4ed8',
  quadrant2TextFill: '#6d28d9',
  quadrant3TextFill: '#b45309',
  quadrant4TextFill: '#047857',
  quadrantPointFill: '#2563eb',
  quadrantPointTextFill: '#0f172a',
  quadrantXAxisTextFill: '#64748b',
  quadrantYAxisTextFill: '#64748b',
}

const THEME_VARIABLES: Record<MermaidThemeName, typeof DARK_THEME_VARIABLES> = {
  dark: DARK_THEME_VARIABLES,
  light: LIGHT_THEME_VARIABLES,
}

/**
 * Resolve Mermaid theme variables for the given theme name.
 * @param theme 'dark' | 'light' (unknown values fall back to dark)
 */
export function getMermaidThemeVariables(theme: MermaidThemeName): typeof DARK_THEME_VARIABLES {
  return THEME_VARIABLES[theme] ?? DARK_THEME_VARIABLES
}

interface ThemeCssTokens {
  text: string
  textSoft: string
  nodeFill: string
  nodeStroke: string
  nodeStrokeHover: string
  edge: string
  edgeGlow: string
  clusterFill: string
  clusterStroke: string
  clusterText: string
  edgeLabelFill: string
  edgeLabelStroke: string
  edgeLabelText: string
  labelOutline: string
  messageText: string
  noteFill: string
  noteStroke: string
  noteText: string
  titleText: string
  legendText: string
  surfaceShadow: string
}

const DARK_CSS_TOKENS: ThemeCssTokens = {
  text: '#f8fafc',
  textSoft: '#e2e8f0',
  nodeFill: '#0c1a2e',
  nodeStroke: '#38bdf8',
  nodeStrokeHover: '#7dd3fc',
  edge: '#38bdf8',
  edgeGlow: 'rgba(56, 189, 248, 0.6)',
  clusterFill: 'rgba(10, 18, 36, 0.65)',
  clusterStroke: 'rgba(56, 189, 248, 0.35)',
  clusterText: '#38bdf8',
  edgeLabelFill: 'rgba(11, 19, 41, 0.95)',
  edgeLabelStroke: 'rgba(56, 189, 248, 0.3)',
  edgeLabelText: '#bae6fd',
  labelOutline: 'rgba(8, 12, 20, 0.85)',
  messageText: '#e2e8f0',
  noteFill: 'rgba(124, 58, 237, 0.2)',
  noteStroke: '#8b5cf6',
  noteText: '#ddd6fe',
  titleText: '#38bdf8',
  legendText: '#cbd5e1',
  surfaceShadow: 'rgba(0, 0, 0, 0.4)',
}

const LIGHT_CSS_TOKENS: ThemeCssTokens = {
  text: '#0f172a',
  textSoft: '#1e293b',
  nodeFill: '#ffffff',
  nodeStroke: '#2563eb',
  nodeStrokeHover: '#1d4ed8',
  edge: '#2563eb',
  edgeGlow: 'rgba(37, 99, 235, 0.45)',
  clusterFill: 'rgba(241, 245, 249, 0.92)',
  clusterStroke: 'rgba(37, 99, 235, 0.3)',
  clusterText: '#1d4ed8',
  edgeLabelFill: 'rgba(255, 255, 255, 0.96)',
  edgeLabelStroke: 'rgba(37, 99, 235, 0.28)',
  edgeLabelText: '#1e3a8a',
  labelOutline: 'rgba(255, 255, 255, 0.92)',
  messageText: '#0f172a',
  noteFill: 'rgba(124, 58, 237, 0.08)',
  noteStroke: '#7c3aed',
  noteText: '#1e1b4b',
  titleText: '#1d4ed8',
  legendText: '#334155',
  surfaceShadow: 'rgba(15, 23, 42, 0.08)',
}

const CSS_TOKENS: Record<MermaidThemeName, ThemeCssTokens> = {
  dark: DARK_CSS_TOKENS,
  light: LIGHT_CSS_TOKENS,
}

/**
 * Build Mermaid `themeCSS` for the given theme.
 *
 * Selectors deliberately carry NO `.mermaid` ancestor prefix: themeCSS is
 * injected into a `<style>` tag *inside* the rendered SVG, where no
 * `.mermaid` wrapper exists. Concrete colors (not `var(--mf-*)`) are used so
 * exported SVG/PNG files keep their contrast outside the app DOM.
 */
export function getMermaidThemeCss(theme: MermaidThemeName): string {
  const t = CSS_TOKENS[theme] ?? DARK_CSS_TOKENS
  return `
  /* ── High-Tech Node Cards & Shapes ── */
  .node rect, .node circle, .node ellipse, .node polygon, .node path {
    fill: ${t.nodeFill} !important;
    stroke: ${t.nodeStroke} !important;
    stroke-width: 1.5px !important;
    rx: 8px !important;
    ry: 8px !important;
    transition: stroke 0.2s ease, filter 0.2s ease;
  }
  .node:hover rect, .node:hover circle, .node:hover ellipse, .node:hover polygon {
    stroke: ${t.nodeStrokeHover} !important;
    stroke-width: 2px !important;
    filter: drop-shadow(0 0 10px ${t.edgeGlow});
  }

  /* ── Subgraphs & Clusters (Group Containers) ── */
  .cluster rect {
    fill: ${t.clusterFill} !important;
    stroke: ${t.clusterStroke} !important;
    stroke-width: 1.5px !important;
    stroke-dasharray: 6 4 !important;
    rx: 12px !important;
    ry: 12px !important;
  }
  .cluster-label text, .cluster text, .cluster-label span, .cluster span, .cluster .nodeLabel {
    font-family: ${CJK_FONT_FAMILY} !important;
    fill: ${t.clusterText} !important;
    color: ${t.clusterText} !important;
    font-weight: 700 !important;
    font-size: 13px !important;
    letter-spacing: 0.5px !important;
  }

  /* ── Flowchart Edges & Connector Lines ── */
  .edgePath .path, .edge-thickness-normal, .flowchart-link {
    stroke: ${t.edge} !important;
    stroke-width: 1.8px !important;
    stroke-linecap: round !important;
    stroke-linejoin: round !important;
    opacity: 0.92;
  }
  .edgePath:hover .path {
    stroke: ${t.nodeStrokeHover} !important;
    stroke-width: 2.2px !important;
    filter: drop-shadow(0 0 6px ${t.edgeGlow});
  }
  .marker, marker path, #flowchart-pointEnd, #statediagram-barbEnd, [id^="flowchart-"] {
    fill: ${t.edge} !important;
    stroke: ${t.edge} !important;
  }

  /* ── Edge Labels & Pills ── */
  .edgeLabel {
    background-color: ${t.edgeLabelFill} !important;
    border: 1px solid ${t.edgeLabelStroke} !important;
    border-radius: 4px !important;
    padding: 2px 6px !important;
    color: ${t.edgeLabelText} !important;
    font-size: 11px !important;
    font-weight: 600 !important;
    box-shadow: 0 2px 6px ${t.surfaceShadow};
  }
  .edgeLabel rect {
    fill: ${t.edgeLabelFill} !important;
    stroke: ${t.edgeLabelStroke} !important;
    rx: 4px !important;
    ry: 4px !important;
  }
  .edgeLabel span, .edgeLabel p {
    font-family: ${CJK_FONT_FAMILY} !important;
    color: ${t.edgeLabelText} !important;
    fill: ${t.edgeLabelText} !important;
    font-weight: 600 !important;
  }
  .edgeLabel .label div, .edgeLabel .label span {
    color: ${t.edgeLabelText} !important;
  }

  /* ── Node Labels & Text Typography ── */
  .node text, .node span, .nodeLabel, .label, .label text, text.actor, .actor > tspan, .labelText, .node .label {
    font-family: ${CJK_FONT_FAMILY} !important;
    fill: ${t.text} !important;
    color: ${t.text} !important;
    font-size: 12.5px !important;
    font-weight: 600 !important;
  }
  foreignObject, foreignObject div, foreignObject span, foreignObject p {
    font-family: ${CJK_FONT_FAMILY} !important;
    color: ${t.text} !important;
    text-align: center;
    word-break: break-word;
    line-height: 1.4 !important;
    font-weight: 600 !important;
  }

  /* ── Sequence Diagrams ── */
  .actor {
    fill: ${t.nodeFill} !important;
    stroke: ${t.nodeStroke} !important;
    stroke-width: 1.5px !important;
    rx: 8px !important;
    ry: 8px !important;
  }
  .actor-line {
    stroke: ${t.clusterStroke} !important;
    stroke-dasharray: 4 4 !important;
    stroke-width: 1.5px !important;
  }
  .messageLine0, .messageLine1 {
    stroke: ${t.edge} !important;
    stroke-width: 1.8px !important;
    stroke-linecap: round !important;
    stroke-linejoin: round !important;
  }
  .messageText {
    fill: ${t.messageText} !important;
    font-size: 12px !important;
    font-weight: 600 !important;
  }
  .note {
    fill: ${t.noteFill} !important;
    stroke: ${t.noteStroke} !important;
    stroke-width: 1.2px !important;
    rx: 6px !important;
    ry: 6px !important;
  }
  .noteText {
    fill: ${t.noteText} !important;
    font-size: 11.5px !important;
    font-weight: 600 !important;
  }

  /* ── Mindmap ── */
  .mindmap-node rect, .mindmap-node circle, .mindmap-node path {
    rx: 8px !important;
    ry: 8px !important;
  }

  /* ── Pie & Titles ── */
  .pieTitleText {
    font-family: ${CJK_FONT_FAMILY} !important;
    fill: ${t.titleText} !important;
    font-weight: 700 !important;
    font-size: 16px !important;
  }
  .legend text {
    font-family: ${CJK_FONT_FAMILY} !important;
    fill: ${t.legendText} !important;
    font-size: 12px !important;
    font-weight: 600 !important;
  }
  .slice text {
    font-family: ${CJK_FONT_FAMILY} !important;
    fill: #ffffff !important;
    font-weight: 700 !important;
  }

  /* ── Class Diagrams ── */
  .classGroup rect {
    fill: ${t.nodeFill} !important;
    stroke: ${t.nodeStroke} !important;
    stroke-width: 1.5px !important;
  }
  .classGroup text, .classGroup .label {
    fill: ${t.text} !important;
    color: ${t.text} !important;
  }
  .classGroup .divider {
    stroke: ${t.nodeStroke} !important;
    stroke-width: 1px !important;
  }
  .relation {
    stroke: ${t.edge} !important;
    stroke-width: 1.8px !important;
  }
  .cardinality, .cardinality text {
    fill: ${t.edgeLabelText} !important;
  }

  /* ── State Diagrams ── */
  .statediagram-state rect, .stateGroup rect {
    fill: ${t.nodeFill} !important;
    stroke: ${t.nodeStroke} !important;
    stroke-width: 1.5px !important;
    rx: 8px !important;
    ry: 8px !important;
  }
  .statediagram-state text, .stateGroup text {
    fill: ${t.text} !important;
    color: ${t.text} !important;
  }
  .statediagram-note rect {
    fill: ${t.noteFill} !important;
    stroke: ${t.noteStroke} !important;
  }
  .statediagram-note text {
    fill: ${t.noteText} !important;
  }
  .transition {
    stroke: ${t.edge} !important;
    stroke-width: 1.8px !important;
  }

  /* ── Entity-Relationship Diagrams ── */
  .er.entityBox {
    fill: ${t.nodeFill} !important;
    stroke: ${t.nodeStroke} !important;
    stroke-width: 1.5px !important;
  }
  .er.entityLabel {
    fill: ${t.clusterText} !important;
    font-weight: 700 !important;
  }
  .er.attributeBoxOdd {
    fill: ${t.clusterFill} !important;
    stroke: ${t.clusterStroke} !important;
  }
  .er.attributeBoxEven {
    fill: ${t.nodeFill} !important;
    stroke: ${t.clusterStroke} !important;
  }
  .er.attributeLabelOdd, .er.attributeLabelEven {
    fill: ${t.text} !important;
    font-size: 11.5px !important;
  }
  .er.relationshipLine {
    stroke: ${t.edge} !important;
    stroke-width: 1.8px !important;
  }
  .er.relationshipLabel {
    fill: ${t.edgeLabelText} !important;
  }

  /* ── Git Diagrams ── */
  .branch-label text, .branchLabel {
    fill: ${t.text} !important;
    font-weight: 600 !important;
  }
  .commit-label {
    fill: ${t.text} !important;
    font-weight: 600 !important;
  }
  .commit-label-bkg {
    fill: ${t.edgeLabelFill} !important;
    stroke: ${t.edgeLabelStroke} !important;
  }

  /* ── Sequence Diagram Overrides (Loops & Activation) ── */
  .labelBox {
    fill: ${t.nodeFill} !important;
    stroke: ${t.nodeStroke} !important;
  }
  .labelText {
    fill: ${t.clusterText} !important;
    font-weight: 700 !important;
  }
  .loopText {
    fill: ${t.text} !important;
  }
  .loopLine {
    stroke: ${t.edge} !important;
    stroke-dasharray: 4 4 !important;
  }

  /* ── 强化图表文字可读性（全局描边兜底） ── */
  svg text, .label text, .nodeLabel, .edgeLabel, .cluster-label text,
  .cluster-label .nodeLabel, .pieTitleText, .pieOuterTitleText, .slice text,
  .legend text, .axisLabel text, .tick text, .titleText, .flowchart-label text,
  .messageText, .noteText {
    font-family: ${CJK_FONT_FAMILY} !important;
    paint-order: stroke fill !important;
    stroke: ${t.labelOutline} !important;
    stroke-width: 2.5px !important;
    stroke-linejoin: round !important;
    stroke-linecap: round !important;
  }

  .pieCircle + .slice text, .pieSection + text {
    stroke: ${t.labelOutline} !important;
    stroke-width: 3px !important;
  }

  .edgeLabel .label rect {
    fill: ${t.edgeLabelFill} !important;
    stroke: ${t.edgeLabelStroke} !important;
  }
  .edgeLabel .label text, .edgeLabel div, .edgeLabel span {
    fill: ${t.edgeLabelText} !important;
    color: ${t.edgeLabelText} !important;
    stroke: ${t.edgeLabelFill} !important;
  }
`
}

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
      const api: MermaidApi = mermaid
      const theme: MermaidThemeName =
        typeof document !== 'undefined' && document.documentElement.getAttribute('data-theme') === 'light'
          ? 'light'
          : 'dark'
      api.initialize({
        startOnLoad: false,
        suppressErrorRendering: true, // 严禁 Mermaid 直接将错误 SVG 写入 DOM
        securityLevel: 'loose',
        theme: 'base',
        themeVariables: getMermaidThemeVariables(theme),
        themeCSS: getMermaidThemeCss(theme),
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

  const themeMode: MermaidThemeName =
    options?.theme ??
    (typeof document !== 'undefined' && document.documentElement.getAttribute('data-theme') === 'light'
      ? 'light'
      : 'dark')

  // mermaid.initialize() 是整体替换而非合并：必须显式传入 themeVariables/themeCSS，
  // 避免二次调用清空配置导致图表文字回落到默认 `#333`。
  const currentConfig = mermaid.mermaidAPI.getConfig()
  const curveType = options?.curve || 'linear'
  mermaid.initialize({
    ...currentConfig,
    theme: 'base',
    themeVariables: getMermaidThemeVariables(themeMode),
    themeCSS: getMermaidThemeCss(themeMode),
    fontFamily: CJK_FONT_FAMILY,
    flowchart: {
      ...(currentConfig?.flowchart || {}),
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
 * @param theme 导出目标主题背景 ('dark' | 'light'，默认当前主题)
 * @returns Promise<void>
 */
export function downloadPng(
  svgContent: string,
  filename = 'minefolio-diagram.png',
  scale = 2,
  theme?: MermaidThemeName
): Promise<void> {
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

      // 绘制背景以契合当前主题模式
      const isLight =
        theme === 'light' ||
        (!theme && typeof document !== 'undefined' && document.documentElement.getAttribute('data-theme') === 'light')
      ctx.fillStyle = isLight ? '#ffffff' : '#0b172a'
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

