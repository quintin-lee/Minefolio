import i18n from '@/composables/useI18n'

/**
 * Legacy module-level translator helpers.
 *
 * These delegate to the vue-i18n global so the dictionaries and the active
 * locale live in exactly one place (see composables/useI18n.ts). Calling
 * `t()` from a render function or a `computed` is reactive because it reads
 * `i18n.global.locale` while the render effect runs.
 */

export function getCurrentLocale(): string {
  return i18n.global.locale.value as string
}

export const t = ((key: string): string => {
  const value = i18n.global.t(key)
  return typeof value === 'string' ? value : key
}) satisfies ((key: string) => string)
