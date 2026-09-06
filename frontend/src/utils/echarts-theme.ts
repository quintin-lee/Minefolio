/**
 * @file ECharts theming helpers
 *
 * ECharts' canvas renderer cannot resolve CSS custom properties — colors like
 * `var(--mf-text-muted)` handed to `setOption` are silently dropped (the canvas
 * falls back to the previously painted color), so chart options must always
 * receive concrete color values.
 *
 * This module reads the active `--mf-*` design tokens from the DOM (which
 * reflect the current `data-theme`) and exposes:
 *  - `resolveChartPalette()`  -> concrete color map for chart options
 *  - `withAlpha()`            -> rgba() variant of a palette color
 *  - `useChartThemeSync()`    -> re-render a chart whenever the theme changes
 */
import { watch } from 'vue'
import { useThemeStore } from '@/stores/theme'

export interface MfChartPalette {
  isLight: boolean
  primary: string
  primaryHover: string
  primaryLight: string
  primaryBorder: string
  accent: string
  surfaceCard: string
  surfaceMuted: string
  surfaceHover: string
  textMain: string
  textRegular: string
  textMuted: string
  textPlaceholder: string
  border: string
  borderSubtle: string
  borderHover: string
  success: string
  danger: string
  warning: string
}

/** Last-resort fallbacks (dark palette) used when a token is missing. */
const DEFAULTS: Omit<MfChartPalette, 'isLight'> = {
  primary: '#3b82f6',
  primaryHover: '#2563eb',
  primaryLight: 'rgba(59, 130, 246, 0.12)',
  primaryBorder: 'rgba(59, 130, 246, 0.28)',
  accent: '#6366f1',
  surfaceCard: 'rgba(15, 23, 42, 0.78)',
  surfaceMuted: 'rgba(30, 41, 59, 0.55)',
  surfaceHover: 'rgba(30, 41, 59, 0.85)',
  textMain: '#f8fafc',
  textRegular: '#cbd5e1',
  textMuted: '#94a3b8',
  textPlaceholder: '#64748b',
  border: 'rgba(59, 130, 246, 0.15)',
  borderSubtle: 'rgba(255, 255, 255, 0.07)',
  borderHover: 'rgba(59, 130, 246, 0.45)',
  success: '#10b981',
  danger: '#f43f5e',
  warning: '#f59e0b',
}

/**
 * Resolve the currently active Minefolio design tokens into concrete CSS
 * color strings usable by the ECharts canvas renderer.
 */
export function resolveChartPalette(): MfChartPalette {
  if (typeof document === 'undefined') return { ...DEFAULTS, isLight: false }
  const cs = getComputedStyle(document.documentElement)
  const get = (name: string, fallback: string): string => {
    const value = cs.getPropertyValue(`--mf-${name}`).trim()
    return value || fallback
  }
  return {
    isLight: document.documentElement.getAttribute('data-theme') === 'light',
    primary: get('primary', DEFAULTS.primary),
    primaryHover: get('primary-hover', DEFAULTS.primaryHover),
    primaryLight: get('primary-light', DEFAULTS.primaryLight),
    primaryBorder: get('primary-border', DEFAULTS.primaryBorder),
    accent: get('accent', DEFAULTS.accent),
    surfaceCard: get('surface-card', DEFAULTS.surfaceCard),
    surfaceMuted: get('surface-muted', DEFAULTS.surfaceMuted),
    surfaceHover: get('surface-hover', DEFAULTS.surfaceHover),
    textMain: get('text-main', DEFAULTS.textMain),
    textRegular: get('text-regular', DEFAULTS.textRegular),
    textMuted: get('text-muted', DEFAULTS.textMuted),
    textPlaceholder: get('text-placeholder', DEFAULTS.textPlaceholder),
    border: get('border', DEFAULTS.border),
    borderSubtle: get('border-subtle', DEFAULTS.borderSubtle),
    borderHover: get('border-hover', DEFAULTS.borderHover),
    success: get('success', DEFAULTS.success),
    danger: get('danger', DEFAULTS.danger),
    warning: get('warning', DEFAULTS.warning),
  }
}

/**
 * Return `color` as an rgba() string with the given alpha. Accepts hex
 * (#rgb/#rrggbb) and rgb()/rgba() input; anything else is returned untouched.
 */
export function withAlpha(color: string, alpha: number): string {
  const trimmed = color.trim()
  const hex = trimmed.match(/^#([0-9a-fA-F]{3}|[0-9a-fA-F]{6})$/)
  if (hex) {
    let hexStr: string = hex[1] ?? ''
    if (hexStr.length === 3) {
      hexStr = hexStr.split('').map((c) => c + c).join('')
    }
    const r = parseInt(hexStr.slice(0, 2), 16)
    const g = parseInt(hexStr.slice(2, 4), 16)
    const b = parseInt(hexStr.slice(4, 6), 16)
    return `rgba(${r}, ${g}, ${b}, ${alpha})`
  }
  const rgb = trimmed.match(/^rgba?\(([^)]+)\)$/)
  if (rgb) {
    const body = rgb[1]
    if (!body) return color
    const parts = body.split(/[,\s]+/).filter(Boolean).map((p) => parseFloat(p))
    const r = parts[0]
    const g = parts[1]
    const b = parts[2]
    if (r !== undefined && g !== undefined && b !== undefined) {
      return `rgba(${r}, ${g}, ${b}, ${alpha})`
    }
  }
  return color
}

/**
 * Return `color` darkened toward black by `factor` (1 = unchanged, 0 = black).
 * Accepts hex (#rgb/#rrggbb) and rgb()/rgba() input; anything else is returned
 * untouched.
 */
export function shade(color: string, factor: number): string {
  const trimmed = color.trim()
  const hex = trimmed.match(/^#([0-9a-fA-F]{3}|[0-9a-fA-F]{6})$/) || null
  if (hex) {
    let hexStr: string = hex[1] ?? ''
    if (hexStr.length === 3) {
      hexStr = hexStr.split('').map((c) => c + c).join('')
    }
    const r = Math.round(parseInt(hexStr.slice(0, 2), 16) * factor)
    const g = Math.round(parseInt(hexStr.slice(2, 4), 16) * factor)
    const b = Math.round(parseInt(hexStr.slice(4, 6), 16) * factor)
    return `rgb(${r}, ${g}, ${b})`
  }
  const rgb = trimmed.match(/^rgba?\(([^)]+)\)$/)
  if (rgb) {
    const body = rgb[1]
    if (!body) return color
    const parts = body.split(/[,\s]+/).filter(Boolean).map((p) => parseFloat(p))
    const r = parts[0]
    const g = parts[1]
    const b = parts[2]
    if (r !== undefined && g !== undefined && b !== undefined) {
      return `rgb(${Math.round(r * factor)}, ${Math.round(g * factor)}, ${Math.round(b * factor)})`
    }
  }
  return color
}

/**
 * Register a watcher that re-renders the chart when the resolved theme
 * (dark/light) changes. Call once inside `<script setup>`.
 */
export function useChartThemeSync(rerender: () => void): void {
  const theme = useThemeStore()
  watch(
    () => theme.resolvedTheme,
    () => rerender(),
  )
}
