/**
 * @file 服务端地址管理 Composable
 * @description 提供移动端服务端 URL 的响应式绑定、校验、持久化及连通性探测功能
 */

import { ref, computed } from 'vue'
import {
  getServerUrl,
  setServerUrl as setHttpServerUrl,
  resetServerUrl as resetHttpServerUrl,
  isCustomServerUrl as isHttpCustomServerUrl,
  testServerConnection,
  normalizeServerUrl,
  isValidServerUrl,
  onServerUrlChange,
  type ServerConnectionTestResult,
} from '@/utils/http'

export type { ServerConnectionTestResult }

const currentServerUrl = ref<string>(getServerUrl())
const isCustom = ref<boolean>(isHttpCustomServerUrl())

// 监听服务端地址全局变更，保持响应式同步
onServerUrlChange(() => {
  currentServerUrl.value = getServerUrl()
  isCustom.value = isHttpCustomServerUrl()
})

export function useServerUrl() {
  /** 格式化后的服务端显示文本 (为空时显示默认内置说明) */
  const displayUrl = computed(() => {
    return currentServerUrl.value || '默认服务 (相对路径)'
  })

  /**
   * 更新服务端地址
   * @param url 新的服务端地址
   */
  function setUrl(url: string): void {
    setHttpServerUrl(url)
  }

  /**
   * 重置服务端地址为系统默认
   */
  function resetUrl(): void {
    resetHttpServerUrl()
  }

  return {
    serverUrl: currentServerUrl,
    isCustom,
    displayUrl,
    setUrl,
    resetUrl,
    testConnection: testServerConnection,
    normalizeServerUrl,
    isValidServerUrl,
  }
}
