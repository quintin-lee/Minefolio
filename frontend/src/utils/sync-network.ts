/**
 * @file 网络状态与前后台生命周期监听工具模块
 * @description 监听移动端网络连通性变化 (Capacitor Network)、浏览器 online 事件及 App 前后台切换 (visibilitychange / appStateChange)，自动触发离线数据增量同步
 */

// frontend/src/utils/sync-network.ts
import { Network } from '@capacitor/network'

/** 同步触发回调函数类型 */
type SyncTrigger = () => void | Promise<void>

interface ListenerHandle {
  remove: () => Promise<void> | void
}

let activeTeardown: (() => void) | null = null

/**
 * 注册跨平台网络状态与应用前后台生命周期监听器（幂等：重复调用会先清理前次注册）
 *
 * 监听触发时机包括：
 * 1. 移动端原生网络状态变更为 connected（Capacitor Network）
 * 2. 浏览器窗口触发 online 事件
 * 3. 页面标签页从后台切回可见前台 (document.visibilitychange)
 * 4. 移动 App 从后台恢复到活跃激活前台 (Capacitor App.appStateChange)
 *
 * @param onSync 当网络恢复或应用回到前台时调用的同步回调函数
 * @returns 反注册清理函数
 */
export function registerNetworkListeners(onSync: SyncTrigger): () => void {
  if (activeTeardown) {
    activeTeardown()
  }

  let disposed = false
  let networkHandle: ListenerHandle | null = null
  let appHandle: ListenerHandle | null = null

  const onOnline = () => {
    if (!disposed) void onSync()
  }
  const onVisibilityChange = () => {
    if (!disposed && document.visibilityState === 'visible') {
      void onSync()
    }
  }

  if (typeof window !== 'undefined') {
    window.addEventListener('online', onOnline)
  }
  if (typeof document !== 'undefined') {
    document.addEventListener('visibilitychange', onVisibilityChange)
  }

  // 原生环境 (Capacitor Network)
  Promise.resolve(
    Network.addListener('networkStatusChange', (status) => {
      if (!disposed && status.connected) void onSync()
    })
  )
    .then((handle) => {
      if (disposed) {
        void handle?.remove?.()
      } else {
        networkHandle = handle ?? null
      }
    })
    .catch(() => {})

  // App 前后台切换 (Capacitor App)
  import('@capacitor/app')
    .then(({ App }) =>
      App.addListener('appStateChange', (state: { isActive: boolean }) => {
        if (!disposed && state.isActive) void onSync()
      })
    )
    .then((handle) => {
      if (disposed) {
        void handle?.remove?.()
      } else {
        appHandle = handle ?? null
      }
    })
    .catch(() => {})

  const teardown = () => {
    if (disposed) return
    disposed = true
    if (activeTeardown === teardown) {
      activeTeardown = null
    }
    if (typeof window !== 'undefined') {
      window.removeEventListener('online', onOnline)
    }
    if (typeof document !== 'undefined') {
      document.removeEventListener('visibilitychange', onVisibilityChange)
    }
    if (networkHandle) {
      void Promise.resolve(networkHandle.remove?.()).catch(() => {})
      networkHandle = null
    }
    if (appHandle) {
      void Promise.resolve(appHandle.remove?.()).catch(() => {})
      appHandle = null
    }
  }

  activeTeardown = teardown
  return teardown
}
