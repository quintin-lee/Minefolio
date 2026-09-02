/**
 * @file 网络状态与前后台生命周期监听工具模块
 * @description 监听移动端网络连通性变化 (Capacitor Network)、浏览器 online 事件及 App 前后台切换 (visibilitychange / appStateChange)，自动触发离线数据增量同步
 */

// frontend/src/utils/sync-network.ts
import { Network } from '@capacitor/network'

/** 同步触发回调函数类型 */
type SyncTrigger = () => void | Promise<void>

/**
 * 注册跨平台网络状态与应用前后台生命周期监听器
 *
 * 监听触发时机包括：
 * 1. 移动端原生网络状态变更为 connected（Capacitor Network）
 * 2. 浏览器窗口触发 online 事件
 * 3. 页面标签页从后台切回可见前台 (document.visibilitychange)
 * 4. 移动 App 从后台恢复到活跃激活前台 (Capacitor App.appStateChange)
 *
 * @param onSync 当网络恢复或应用回到前台时调用的同步回调函数
 */
export function registerNetworkListeners(onSync: SyncTrigger): void {
  // 原生环境 (Capacitor Network)
  Network.addListener('networkStatusChange', (status) => {
    if (status.connected) void onSync()
  })

  // Web / 浏览器兜底
  window.addEventListener('online', () => void onSync())
  window.addEventListener('offline', () => {})

  // 页面从后台切回前台 → 增量同步
  document.addEventListener('visibilitychange', () => {
    if (document.visibilityState === 'visible') void onSync()
  })

  // App 前后台切换 (Capacitor App)
  import('@capacitor/app')
    .then(({ App }) => {
      App.addListener('appStateChange', (state: { isActive: boolean }) => {
        if (state.isActive) void onSync()
      })
    })
    .catch(() => {})
}

