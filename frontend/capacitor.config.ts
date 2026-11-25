import type { CapacitorConfig } from '@capacitor/cli'

// Production APK builds default to strict HTTPS without cleartext or mixed content.
// Local LAN HTTP debugging can explicitly opt in with CAP_ALLOW_CLEARTEXT=true or NODE_ENV=development.
const allowCleartextDev =
  process.env.CAP_ALLOW_CLEARTEXT === 'true' || process.env.NODE_ENV === 'development'

const config: CapacitorConfig = {
  appId: 'com.minefolio.app',
  appName: 'Minefolio',
  webDir: 'dist-mobile',
  server: {
    androidScheme: 'https',
    ...(allowCleartextDev ? { cleartext: true } : {}),
  },
  plugins: {
    SplashScreen: {
      launchAutoHide: false,
      backgroundColor: '#060b18',
      showSpinner: false,
      splashFullScreen: false,
      splashImmersive: false,
    },
  },
  android: {
    allowMixedContent: allowCleartextDev,
    backgroundColor: '#060b18',
  },
}

export default config
