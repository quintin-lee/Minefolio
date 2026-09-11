# Application Logo Image Assets & Mobile App Icons Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Regenerate all web favicon/PWA binary image assets and Android Capacitor mobile app icons (launcher icons, adaptive icons, round icons, splash screens) using the newly designed 3D faceted "M" branding, and integrate the brand logo into mobile views.

**Architecture:** Create an automated asset generation script leveraging `rsvg-convert` and ImageMagick (`magick`) to render high-DPI raster assets from `favicon.svg` across web and Android mipmap resolutions. Update web manifests and HTML head references so browser tabs and home screens show crisp, branded icons. Integrate `<AppLogo>` into `LoginMobile.vue` and enhance mobile dashboard branding.

**Tech Stack:** SVG, ImageMagick 7 (`magick`), `librsvg` (`rsvg-convert`), Android Adaptive Icons XML, Vue 3, TypeScript, Vite.

---

### File Structure & Changes

| Path | Action | Description |
| :--- | :--- | :--- |
| `frontend/scripts/generate-icons.sh` | Create | Bash automation script to render all web and Android icon assets from SVG |
| `frontend/public/favicon-16x16.png` | Modify (Binary) | 16x16 crisp favicon PNG for browser tabs |
| `frontend/public/favicon-32x32.png` | Modify (Binary) | 32x32 standard favicon PNG for browser tabs |
| `frontend/public/favicon.ico` | Create (Binary) | Multi-resolution (16, 32, 48) Windows icon resource |
| `frontend/public/apple-touch-icon.png` | Modify (Binary) | 180x180 iOS touch icon |
| `frontend/public/android-chrome-192x192.png` | Create (Binary) | 192x192 PWA launcher icon |
| `frontend/public/android-chrome-512x512.png` | Create (Binary) | 512x512 PWA splash/maskable icon |
| `frontend/public/site.webmanifest` | Modify | Declare 192x192 and 512x512 PWA icons |
| `frontend/index.html` | Modify | Add `favicon.ico` fallback link |
| `frontend/index.mobile.html` | Modify | Add `favicon.ico` fallback link |
| `frontend/android/app/src/main/res/mipmap-*/ic_launcher.png` | Modify (Binary) | Android square launcher icons (mdpi 48 to xxxhdpi 192) |
| `frontend/android/app/src/main/res/mipmap-*/ic_launcher_round.png` | Modify (Binary) | Android circular launcher icons (mdpi 48 to xxxhdpi 192) |
| `frontend/android/app/src/main/res/mipmap-*/ic_launcher_foreground.png` | Modify (Binary) | Android adaptive icon foregrounds (mdpi 108 to xxxhdpi 432) |
| `frontend/android/app/src/main/res/drawable/ic_launcher_background.xml` | Modify | Vector background for adaptive icon (#060B18 dark slate) |
| `frontend/android/app/src/main/res/drawable*/splash.png` | Modify (Binary) | Mobile splash screens centered with 3D M logo on #060B18 |
| `frontend/src/views-mobile/LoginMobile.vue` | Modify | Replace plain text title with `<AppLogo :size="48" :with-text="true" />` |
| `frontend/src/views-mobile/DashboardMobile.vue` | Modify | Integrate brand mark in mobile header bar |

---

### Task 1: Generate Web Favicon & PWA Multi-Resolution Assets

**Files:**
- Create: `frontend/scripts/generate-icons.sh`
- Generate: `frontend/public/favicon-16x16.png`
- Generate: `frontend/public/favicon-32x32.png`
- Generate: `frontend/public/favicon.ico`
- Generate: `frontend/public/apple-touch-icon.png`
- Generate: `frontend/public/android-chrome-192x192.png`
- Generate: `frontend/public/android-chrome-512x512.png`
- Modify: `frontend/public/site.webmanifest`
- Modify: `frontend/index.html`
- Modify: `frontend/index.mobile.html`

- [x] **Step 1: Write `frontend/scripts/generate-icons.sh` for web assets**
  Implement script section that:
  - Renders 16x16 and 32x32 PNGs from `frontend/public/favicon.svg` using `rsvg-convert`
  - Generates 48x48 temporary PNG and bundles 16x16, 32x32, 48x48 into `frontend/public/favicon.ico` using `magick`
  - Renders 180x180 `apple-touch-icon.png`, 192x192 `android-chrome-192x192.png`, and 512x512 `android-chrome-512x512.png`

- [x] **Step 2: Execute script and verify web image files**
  Run: `chmod +x frontend/scripts/generate-icons.sh && ./frontend/scripts/generate-icons.sh web`
  Verify: `file frontend/public/favicon*.png frontend/public/favicon.ico frontend/public/apple-touch-icon.png frontend/public/android-chrome-*.png`

- [x] **Step 3: Update `site.webmanifest` and HTML files**
  - Add `android-chrome-192x192.png` and `android-chrome-512x512.png` entries in `site.webmanifest`
  - Include `<link rel="shortcut icon" href="/favicon.ico" />` in `index.html` and `index.mobile.html`

- [x] **Step 4: Commit web icon assets**
  Commit with message: `feat(branding): 🎨 regenerate web favicon PNGs, multi-res favicon.ico and PWA icons`

---

### Task 2: Regenerate Android Launcher & Adaptive Mobile Icons

**Files:**
- Modify: `frontend/scripts/generate-icons.sh`
- Modify: `frontend/android/app/src/main/res/drawable/ic_launcher_background.xml`
- Generate: `frontend/android/app/src/main/res/mipmap-*/ic_launcher.png` (5 densities)
- Generate: `frontend/android/app/src/main/res/mipmap-*/ic_launcher_round.png` (5 densities)
- Generate: `frontend/android/app/src/main/res/mipmap-*/ic_launcher_foreground.png` (5 densities)
- Generate: `frontend/android/app/src/main/res/drawable*/splash.png` (11 resolutions)

- [x] **Step 1: Extend `generate-icons.sh` for Android mipmap and drawable assets**
  - Standard launcher icons (`ic_launcher.png`):
    - mdpi: 48x48, hdpi: 72x72, xhdpi: 96x96, xxhdpi: 144x144, xxxhdpi: 192x192
  - Circular launcher icons (`ic_launcher_round.png`):
    - Apply circular mask to full icon across all 5 densities
  - Adaptive foreground icons (`ic_launcher_foreground.png`):
    - Render icon centered with safe-zone padding (~66.7% size within 108dp viewport)
    - mdpi: 108x108, hdpi: 162x162, xhdpi: 216x216, xxhdpi: 324x324, xxxhdpi: 432x432
  - Splash screens (`splash.png`):
    - Center the 3D M brand logo on `#060B18` canvas matching each splash density dimension

- [x] **Step 2: Update `ic_launcher_background.xml`**
  Ensure background vectors harmonize with `#060B18` dark theme.

- [x] **Step 3: Execute Android icon generation**
  Run: `./frontend/scripts/generate-icons.sh android`
  Verify all files exist and match expected dimensions:
  `file frontend/android/app/src/main/res/mipmap-*/*.png`

- [x] **Step 4: Commit Android icon assets**
  Commit with message: `feat(mobile): 📱 regenerate Android launcher, adaptive icons, and splash screens`

---

### Task 3: Mobile UI Branding Integration & Full Build Verification

**Files:**
- Modify: `frontend/src/views-mobile/LoginMobile.vue`
- Modify: `frontend/src/views-mobile/DashboardMobile.vue`
- Test: Vitest unit tests & Vite desktop/mobile builds

- [x] **Step 1: Integrate `<AppLogo>` in `LoginMobile.vue`**
  Replace `<h1 class="brand">Minefolio</h1>` with:
  ```vue
  <div class="brand-header">
    <AppLogo :size="44" :with-text="true" />
  </div>
  ```

- [x] **Step 2: Add brand mark in `DashboardMobile.vue`**
  Add subtle `<AppLogo :size="24" :with-text="true" />` in mobile dashboard header.

- [x] **Step 3: Run comprehensive verification**
  - Run frontend test suite: `npm --prefix frontend test`
  - Run desktop build: `npm --prefix frontend run build`
  - Run mobile build: `npm --prefix frontend run build:mobile`

- [x] **Step 4: Commit UI integration**
  Commit with message: `feat(mobile): 🎨 integrate AppLogo into mobile login and dashboard header`
