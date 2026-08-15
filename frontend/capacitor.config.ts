import type { CapacitorConfig } from '@capacitor/cli'

const config: CapacitorConfig = {
  appId: 'com.minefolio.app',
  appName: 'Minefolio',
  webDir: 'dist-mobile',
  server: {
    androidScheme: 'https',
  },
}

export default config
