import type { CapacitorConfig } from '@capacitor/cli'

const config: CapacitorConfig = {
  appId: 'com.minefolio.app',
  appName: 'Minefolio',
  webDir: 'dist-mobile',
  server: {
    androidScheme: 'https',
    // 局域网联调常用 http://192.168.x.x:8080 明文地址：
    // 允许 WebView 发起 http 请求（配合 AndroidManifest 的 usesCleartextTraffic）。
    cleartext: true,
  },
  android: {
    // 允许 https origin（WebView 本体）加载 http 后端接口，解决 Mixed Content 拦截。
    allowMixedContent: true,
  },
}

export default config
