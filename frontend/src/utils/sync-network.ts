// frontend/src/utils/sync-network.ts
import { Network } from '@capacitor/network'

type SyncTrigger = () => void | Promise<void>

export function registerNetworkListeners(onSync: SyncTrigger): void {
  // 原生环境
  Network.addListener('networkStatusChange', (status) => {
    if (status.connected) void onSync()
  })

  // Web / 兜底
  window.addEventListener('online', () => void onSync())
  window.addEventListener('offline', () => {})

  // 页面从后台切回前台 → 增量同步
  document.addEventListener('visibilitychange', () => {
    if (document.visibilityState === 'visible') void onSync()
  })

  // app 前后台（Capacitor）
  import('@capacitor/app')
    .then(({ App }) => {
      App.addListener('appStateChange', (state: { isActive: boolean }) => {
        if (state.isActive) void onSync()
      })
    })
    .catch(() => {})
}
