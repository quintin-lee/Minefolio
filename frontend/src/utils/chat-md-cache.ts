/**
 * Module-level caches for rendered markdown, Mermaid SVG, highlighted code, and linked financial entities.
 * Cleared explicitly on auth logout to prevent cross-account HTML/entity leakage.
 */
export const sharedMdCache = new Map<string, string>()
export const linkedMdCache = new Map<string, string>()
export const mermaidSvgCache = new Map<string, string>()
export const codeHighlightCache = new Map<string, string>()

export const MERMAID_SVG_CACHE_LIMIT = 80
export const CODE_HIGHLIGHT_CACHE_LIMIT = 120

export function clearChatMarkdownCaches(): void {
  sharedMdCache.clear()
  linkedMdCache.clear()
  mermaidSvgCache.clear()
  codeHighlightCache.clear()
}
