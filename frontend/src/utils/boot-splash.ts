import { Capacitor } from '@capacitor/core'
import { SplashScreen } from '@capacitor/splash-screen'

export const BOOT_SPLASH_ID = 'mf-boot-splash'

/**
 * Dismiss the Capacitor native SplashScreen so the HTML inline boot animation
 * takes over seamlessly while local SQLite WASM & router initialize.
 */
export async function hideNativeSplash(): Promise<void> {
  try {
    if (Capacitor.isNativePlatform()) {
      await SplashScreen.hide({ fadeOutDuration: 150 })
    }
  } catch {
    // Ignore when SplashScreen plugin is unavailable (e.g. web preview / tests)
  }
}

/**
 * Fade out and remove the inline HTML boot splash overlay once Vue has mounted
 * and the initial route is ready. Pass `immediate = true` on bootstrap failure
 * so error messages are immediately visible.
 */
export function dismissBootSplash(immediate = false): void {
  if (typeof document === 'undefined') return
  const el = document.getElementById(BOOT_SPLASH_ID)
  if (!el) return

  if (immediate) {
    el.remove()
    return
  }

  if (el.classList.contains('is-leaving')) return
  el.classList.add('is-leaving')

  let removed = false
  const cleanup = () => {
    if (removed) return
    removed = true
    el.remove()
  }

  el.addEventListener('transitionend', cleanup, { once: true })
  setTimeout(cleanup, 360)
}
