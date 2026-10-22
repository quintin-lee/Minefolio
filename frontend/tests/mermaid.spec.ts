import { describe, it, expect } from 'vitest'
import {
  detectDiagramType,
  sanitizeMermaid,
  sanitizeMermaidSvg,
  getMermaidThemeVariables,
  getMermaidThemeCss,
} from '@/utils/mermaid'

describe('Mermaid utilities & theming', () => {
  describe('detectDiagramType', () => {
    it('detects flowchart, pie, sequence, state, class, and er diagrams', () => {
      expect(detectDiagramType('flowchart TD\n  A --> B').type).toBe('flowchart')
      expect(detectDiagramType('graph LR\n  A --> B').type).toBe('flowchart')
      expect(detectDiagramType('pie title 资产配置\n  "股票": 60').type).toBe('pie')
      expect(detectDiagramType('sequenceDiagram\n  Alice->>Bob: Hello').type).toBe('sequence')
      expect(detectDiagramType('classDiagram\n  class BankAccount').type).toBe('class')
      expect(detectDiagramType('stateDiagram-v2\n  [*] --> Still').type).toBe('state')
      expect(detectDiagramType('erDiagram\n  USER ||--o{ ORDER : places').type).toBe('er')
      expect(detectDiagramType('gitGraph\n  commit').type).toBe('git')
      expect(detectDiagramType('mindmap\n  root((中心))').type).toBe('mindmap')
    })
  })

  describe('sanitizeMermaid', () => {
    it('strips markdown code blocks', () => {
      const input = '```mermaid\nflowchart TD\nA --> B\n```'
      expect(sanitizeMermaid(input)).toBe('flowchart TD\nA --> B')
    })

    it('fixes unquoted brackets containing parentheses in Chinese node labels', () => {
      const input = 'flowchart TD\n  A[应急金 (3-6月)] --> B'
      expect(sanitizeMermaid(input)).toBe('flowchart TD\n  A["应急金 (3-6月)"] --> B')
    })
  })

  describe('sanitizeMermaidSvg', () => {
    it('preserves foreignObject and style tags while stripping dangerous scripts', () => {
      const svg = '<svg><style>.node{fill:#fff;}</style><foreignObject><div>Label</div></foreignObject><script>alert(1)</script></svg>'
      const sanitized = sanitizeMermaidSvg(svg)
      expect(sanitized).toContain('<style>')
      expect(sanitized.toLowerCase()).toContain('<foreignobject')
      expect(sanitized).not.toContain('<script')
    })
  })

  describe('Theme variables & contrast', () => {
    it('provides high contrast theme variables for dark mode', () => {
      const darkVars = getMermaidThemeVariables('dark')
      expect(darkVars.darkMode).toBe(true)
      expect(darkVars.textColor).toBe('#f8fafc')
      expect(darkVars.nodeTextColor).toBe('#f8fafc')
      expect(darkVars.pieSectionTextColor).toBe('#ffffff')
      expect(darkVars.pieLegendTextColor).toBe('#f8fafc')
    })

    it('provides high contrast theme variables for light mode', () => {
      const lightVars = getMermaidThemeVariables('light')
      expect(lightVars.darkMode).toBe(false)
      expect(lightVars.textColor).toBe('#0f172a')
      expect(lightVars.nodeTextColor).toBe('#0f172a')
      expect(lightVars.pieLegendTextColor).toBe('#0f172a')
      expect(lightVars.mainBkg).toBe('#ffffff')
    })
  })

  describe('Theme CSS generation', () => {
    it('generates valid CSS without unclosed braces or .mermaid prefix', () => {
      const darkCss = getMermaidThemeCss('dark')
      expect(darkCss).not.toContain('.mermaid ')
      expect(darkCss).not.toContain('.mermaid.')
      // Verify balanced braces
      const openCount = (darkCss.match(/\{/g) || []).length
      const closeCount = (darkCss.match(/\}/g) || []).length
      expect(openCount).toBe(closeCount)

      // Verify paint-order for contrast
      expect(darkCss).toContain('paint-order: stroke fill')
      expect(darkCss).toContain('.slice text')
      expect(darkCss).toContain('.edgeLabel')
      expect(darkCss).toContain('.messageText')
    })

    it('generates light theme CSS with dark text and light surface strokes', () => {
      const lightCss = getMermaidThemeCss('light')
      const openCount = (lightCss.match(/\{/g) || []).length
      const closeCount = (lightCss.match(/\}/g) || []).length
      expect(openCount).toBe(closeCount)

      expect(lightCss).toContain('#0f172a')
      expect(lightCss).toContain('paint-order: stroke fill')
    })
  })
})
