import { defineConfig } from 'vitest/config'
import vue from '@vitejs/plugin-vue'
import AutoImport from 'unplugin-auto-import/vite'
import Components from 'unplugin-vue-components/vite'
import { ElementPlusResolver } from 'unplugin-vue-components/resolvers'
import { resolve } from 'path'
import { readFileSync } from 'fs'

const version = JSON.parse(readFileSync(resolve(__dirname, 'package.json'), 'utf-8')).version

// Mobile build target: separate entry HTML + dist-mobile output.
// `__IS_MOBILE__` lets Vite tree-shake mobile-only offline DB code out of desktop bundles.
// dts paths are mobile-specific to avoid clobbering the desktop build's .d.ts.
export default defineConfig({
  plugins: [
    vue(),
    AutoImport({
      resolvers: [ElementPlusResolver({ importStyle: false })],
      imports: ['vue', 'vue-router', 'pinia', 'vue-i18n'],
      dts: 'src/auto-imports.mobile.d.ts',
    }),
    Components({
      resolvers: [ElementPlusResolver({ importStyle: false })],
      dts: 'src/components.mobile.d.ts',
    }),
    {
      // Dev server normally falls back to index.html (desktop entry). Route
      // mobile navigations to the mobile entry so offline DB/sync boot runs.
      name: 'mobile-html-fallback',
      configureServer(server) {
        server.middlewares.use((req, _res, next) => {
          const url = req.url || ''
          const wantsHtml = (req.headers.accept || '').includes('text/html')
          if (
            wantsHtml &&
            !url.startsWith('/@') &&
            !url.startsWith('/api/') &&
            !url.includes('.')
          ) {
            req.url = '/index.mobile.html'
          }
          next()
        })
      },
    },
  ],
  define: {
    __APP_VERSION__: JSON.stringify(version),
    __IS_MOBILE__: true,
  },
  resolve: {
    alias: { '@': resolve(__dirname, 'src') },
  },
  server: {
    port: 5174,
    proxy: { '/api': { target: 'http://localhost:8080', changeOrigin: true } },
  },
  build: {
    outDir: 'dist-mobile',
    emptyOutDir: true,
    rollupOptions: {
      input: resolve(__dirname, 'index.mobile.html'),
      output: {
        manualChunks(id) {
          if (id.includes('sql.js') || id.includes('sql-wasm-base64')) {
            return 'vendor-sql'
          }
          if (id.includes('node_modules')) {
            if (id.includes('echarts') || id.includes('zrender')) {
              return 'vendor-echarts'
            }
            if (id.includes('element-plus') || id.includes('@element-plus/icons-vue')) {
              return 'vendor-element'
            }
            if (id.includes('highlight.js')) {
              return 'vendor-hljs'
            }
            if (id.includes('katex') || id.includes('marked') || id.includes('dompurify')) {
              return 'vendor-markdown'
            }
          }
        },
      },
    },
  },
  test: {
    environment: 'jsdom',
    globals: true,
    include: ['tests/**/*.spec.ts'],
    setupFiles: ['tests/setup.ts'],
    testTimeout: 10000,
    coverage: {
      provider: 'v8',
      reporter: ['text', 'html', 'lcov'],
      reportsDirectory: 'coverage',
      include: ['src/**/*.{ts,vue}'],
      exclude: ['src/**/*.d.ts', 'src/db/generated/**', 'src/icons/**', 'src/styles/**'],
      thresholds: { lines: 50, functions: 42, branches: 65, statements: 50 },
    },
  },
})
