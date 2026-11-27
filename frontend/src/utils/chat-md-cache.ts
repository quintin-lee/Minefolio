/**
 * Module-level caches for rendered markdown and linked financial entities.
 * Cleared explicitly on auth logout to prevent cross-account HTML/entity leakage.
 */
export const sharedMdCache = new Map<string, string>()
export const linkedMdCache = new Map<string, string>()

export function clearChatMarkdownCaches(): void {
  sharedMdCache.clear()
  linkedMdCache.clear()
}
