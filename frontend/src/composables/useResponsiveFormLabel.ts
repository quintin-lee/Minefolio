import { ref, computed, onMounted, onUnmounted } from 'vue'

const COMPACT_MEDIA_QUERY = '(max-width: 640px)'

export function useResponsiveFormLabel(mediaQuery: string = COMPACT_MEDIA_QUERY) {
  const isCompact = ref(false)
  let mql: MediaQueryList | null = null

  const update = (matches?: boolean) => {
    isCompact.value = typeof matches === 'boolean' ? matches : Boolean(mql?.matches)
  }

  const handleChange = (e: MediaQueryListEvent) => {
    update(e.matches)
  }

  if (typeof window !== 'undefined' && typeof window.matchMedia === 'function') {
    mql = window.matchMedia(mediaQuery)
    update(mql.matches)
  }

  onMounted(() => {
    if (!mql && typeof window !== 'undefined' && typeof window.matchMedia === 'function') {
      mql = window.matchMedia(mediaQuery)
    }
    if (!mql) return
    update(mql.matches)
    if (typeof mql.addEventListener === 'function') {
      mql.addEventListener('change', handleChange)
    } else if (typeof mql.addListener === 'function') {
      mql.addListener(handleChange)
    }
  })

  onUnmounted(() => {
    if (!mql) return
    if (typeof mql.removeEventListener === 'function') {
      mql.removeEventListener('change', handleChange)
    } else if (typeof mql.removeListener === 'function') {
      mql.removeListener(handleChange)
    }
  })

  const labelPosition = computed<'top' | 'right'>(() => (isCompact.value ? 'top' : 'right'))

  return {
    isCompact,
    labelPosition,
  }
}
