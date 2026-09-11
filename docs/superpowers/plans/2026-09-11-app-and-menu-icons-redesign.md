# App Logo and Menu Icons Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Redesign Minefolio's application brand logo with a 3D faceted "M" and momentum trendline, and unify all desktop and mobile navigation menus with Phosphor Duotone dual-tone icons.

**Architecture:** Create an SVG-based reusable `<AppLogo>` component and vector `favicon.svg`. Modernize the desktop sidebar in `Layout.vue` and the mobile bottom tab bar in `MobileLayout.vue` using `@iconify/vue` with Phosphor Duotone (`ph:*-duotone`) icons. Integrate the brand logo into `Login.vue` and `Setup.vue`.

**Tech Stack:** Vue 3 (Composition API, `<script setup>`), TypeScript, `@iconify/vue` (Phosphor Duotone), Vite, Vitest, Element Plus.

---

### Task 1: Create Reusable `<AppLogo>` Component with Unit Tests

**Files:**
- Create: `frontend/src/components/AppLogo.vue`
- Create: `frontend/tests/app-logo.spec.ts`

- [ ] **Step 1: Write the failing unit test for `<AppLogo>`**

```ts
// frontend/tests/app-logo.spec.ts
import { describe, it, expect } from 'vitest'
import { mount } from '@vue/test-utils'
import AppLogo from '@/components/AppLogo.vue'

describe('AppLogo.vue', () => {
  it('renders SVG icon with default size', () => {
    const wrapper = mount(AppLogo)
    const svg = wrapper.find('svg')
    expect(svg.exists()).toBe(true)
    expect(svg.attributes('viewBox')).toBe('0 0 512 512')
    expect(wrapper.find('.logo-title').exists()).toBe(false)
  })

  it('renders brand text when withText is true', () => {
    const wrapper = mount(AppLogo, {
      props: {
        withText: true,
        size: 36,
      },
    })
    const title = wrapper.find('.logo-title')
    expect(title.exists()).toBe(true)
    expect(title.text()).toBe('Minefolio')
  })

  it('applies custom size style', () => {
    const wrapper = mount(AppLogo, {
      props: {
        size: 48,
      },
    })
    const iconWrapper = wrapper.find('.app-logo-icon')
    expect(iconWrapper.attributes('style')).toContain('48px')
  })
})
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd frontend && npm test tests/app-logo.spec.ts`
Expected: FAIL (Cannot find module `@/components/AppLogo.vue`)

- [ ] **Step 3: Implement `AppLogo.vue`**

```vue
<!-- frontend/src/components/AppLogo.vue -->
<template>
  <div class="app-logo" :class="{ 'is-animated': animated }">
    <div class="app-logo-icon" :style="{ width: `${parsedSize}px`, height: `${parsedSize}px` }">
      <svg
        xmlns="http://www.w3.org/2000/svg"
        viewBox="0 0 512 512"
        class="logo-svg"
        fill="none"
      >
        <defs>
          <!-- Background shield gradient -->
          <radialGradient id="mfLogoBg" cx="50%" cy="25%" r="80%">
            <stop offset="0%" stop-color="#0e1b33" />
            <stop offset="100%" stop-color="#060b18" />
          </radialGradient>

          <!-- Left pillar gradient: Solid asset base (Indigo to Violet) -->
          <linearGradient id="mfLeftPillar" x1="0%" y1="100%" x2="0%" y2="0%">
            <stop offset="0%" stop-color="#4338ca" />
            <stop offset="50%" stop-color="#6366f1" />
            <stop offset="100%" stop-color="#818cf8" />
          </linearGradient>

          <!-- Left bevel / inner depth -->
          <linearGradient id="mfLeftInner" x1="0%" y1="0%" x2="100%" y2="100%">
            <stop offset="0%" stop-color="#312e81" />
            <stop offset="100%" stop-color="#1e1b4b" />
          </linearGradient>

          <!-- Right momentum trendline: Aurora Cyan to Sky Blue -->
          <linearGradient id="mfRightSurge" x1="0%" y1="100%" x2="100%" y2="0%">
            <stop offset="0%" stop-color="#0284c7" />
            <stop offset="45%" stop-color="#00d4ff" />
            <stop offset="100%" stop-color="#38bdf8" />
          </linearGradient>

          <!-- Right inner bevel -->
          <linearGradient id="mfRightInner" x1="100%" y1="100%" x2="0%" y2="0%">
            <stop offset="0%" stop-color="#0369a1" />
            <stop offset="100%" stop-color="#082f49" />
          </linearGradient>

          <!-- Peak glow filter -->
          <filter id="mfGlow" x="-20%" y="-20%" width="140%" height="140%">
            <feGaussianBlur stdDeviation="8" result="blur" />
            <feComposite in="SourceGraphic" in2="blur" operator="over" />
          </filter>
        </defs>

        <!-- Rounded shield backing -->
        <rect x="24" y="24" width="464" height="464" rx="112" fill="url(#mfLogoBg)" />
        <rect
          x="24"
          y="24"
          width="464"
          height="464"
          rx="112"
          fill="none"
          stroke="rgba(0, 212, 255, 0.25)"
          stroke-width="6"
        />

        <!-- Ambient glow inside shield -->
        <circle cx="360" cy="180" r="90" fill="#00d4ff" opacity="0.12" filter="url(#mfGlow)" />
        <circle cx="160" cy="340" r="100" fill="#6366f1" opacity="0.15" filter="url(#mfGlow)" />

        <!-- 3D Geometric Faceted "M" -->
        <!-- Left Pillar Face -->
        <path
          d="M100 376 V176 L164 136 V376 H100 Z"
          fill="url(#mfLeftPillar)"
        />

        <!-- Left Inward Slope (Depth shadow) -->
        <path
          d="M164 136 L248 296 L220 336 L164 230 V376 H164 Z"
          fill="url(#mfLeftInner)"
          opacity="0.9"
        />

        <!-- Center V-Peak reflection -->
        <path
          d="M164 136 L248 296 L276 244 L196 116 Z"
          fill="#818cf8"
          opacity="0.8"
        />

        <!-- Right Rising Momentum Diagonal (Center valley to peak) -->
        <path
          d="M248 296 L364 120 L404 146 L276 348 Z"
          fill="url(#mfRightSurge)"
        />

        <!-- Right Inward Slope (Depth) -->
        <path
          d="M276 348 L404 146 L380 376 H330 L310 270 Z"
          fill="url(#mfRightInner)"
        />

        <!-- Right Pillar Wing -->
        <path
          d="M344 376 V224 L408 140 V376 H344 Z"
          fill="url(#mfRightSurge)"
        />

        <!-- Upward Momentum Trend Arrow at top-right -->
        <g filter="url(#mfGlow)">
          <path
            d="M380 108 L436 104 L432 160 L408 136 L366 182 L344 160 L386 114 Z"
            fill="#38bdf8"
          />
        </g>
      </svg>
    </div>

    <span v-if="withText" class="logo-title">Minefolio</span>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'

const props = withDefaults(
  defineProps<{
    size?: number | string
    withText?: boolean
    animated?: boolean
  }>(),
  {
    size: 32,
    withText: false,
    animated: true,
  },
)

const parsedSize = computed(() => {
  const s = typeof props.size === 'string' ? parseInt(props.size, 10) : props.size
  return Number.isNaN(s) || s <= 0 ? 32 : s
})
</script>

<style scoped>
.app-logo {
  display: inline-flex;
  align-items: center;
  gap: 10px;
  user-select: none;
  line-height: 1;
}

.app-logo-icon {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
  transition: transform 0.25s cubic-bezier(0.34, 1.56, 0.64, 1), filter 0.25s ease;
}

.logo-svg {
  width: 100%;
  height: 100%;
  display: block;
}

.logo-title {
  font-size: 19px;
  font-weight: 700;
  letter-spacing: 0.8px;
  background: linear-gradient(135deg, var(--mf-primary, #00d4ff) 0%, #a78bfa 100%);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
  font-family: inherit;
}

.app-logo.is-animated:hover .app-logo-icon {
  transform: translateY(-2px) scale(1.06);
  filter: drop-shadow(0 4px 12px rgba(0, 212, 255, 0.35));
}
</style>
```

- [ ] **Step 4: Run unit test to verify it passes**

Run: `cd frontend && npm test tests/app-logo.spec.ts`
Expected: PASS (All 3 tests pass)

- [ ] **Step 5: Commit Task 1**

```bash
git add frontend/src/components/AppLogo.vue frontend/tests/app-logo.spec.ts
git commit -m "feat(branding): ✨ create reusable AppLogo component with 3D faceted M and momentum surge"
```

---

### Task 2: Update `favicon.svg` and Public Manifest with the New Brand Visuals

**Files:**
- Modify: `frontend/public/favicon.svg`

- [ ] **Step 1: Replace `frontend/public/favicon.svg` with high-contrast modern FinTech Cyber vector**

```xml
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512" width="512" height="512">
  <defs>
    <!-- Background shield gradient -->
    <radialGradient id="mfLogoBg" cx="50%" cy="25%" r="80%">
      <stop offset="0%" stop-color="#0e1b33" />
      <stop offset="100%" stop-color="#060b18" />
    </radialGradient>

    <!-- Left pillar gradient: Solid asset base (Indigo to Violet) -->
    <linearGradient id="mfLeftPillar" x1="0%" y1="100%" x2="0%" y2="0%">
      <stop offset="0%" stop-color="#4338ca" />
      <stop offset="50%" stop-color="#6366f1" />
      <stop offset="100%" stop-color="#818cf8" />
    </linearGradient>

    <!-- Left bevel / inner depth -->
    <linearGradient id="mfLeftInner" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" stop-color="#312e81" />
      <stop offset="100%" stop-color="#1e1b4b" />
    </linearGradient>

    <!-- Right momentum trendline: Aurora Cyan to Sky Blue -->
    <linearGradient id="mfRightSurge" x1="0%" y1="100%" x2="100%" y2="0%">
      <stop offset="0%" stop-color="#0284c7" />
      <stop offset="45%" stop-color="#00d4ff" />
      <stop offset="100%" stop-color="#38bdf8" />
    </linearGradient>

    <!-- Right inner bevel -->
    <linearGradient id="mfRightInner" x1="100%" y1="100%" x2="0%" y2="0%">
      <stop offset="0%" stop-color="#0369a1" />
      <stop offset="100%" stop-color="#082f49" />
    </linearGradient>

    <!-- Peak glow filter -->
    <filter id="mfGlow" x="-20%" y="-20%" width="140%" height="140%">
      <feGaussianBlur stdDeviation="8" result="blur" />
      <feComposite in="SourceGraphic" in2="blur" operator="over" />
    </filter>
  </defs>

  <!-- Rounded shield backing -->
  <rect x="24" y="24" width="464" height="464" rx="112" fill="url(#mfLogoBg)" />
  <rect
    x="24"
    y="24"
    width="464"
    height="464"
    rx="112"
    fill="none"
    stroke="rgba(0, 212, 255, 0.25)"
    stroke-width="6"
  />

  <!-- Ambient glow inside shield -->
  <circle cx="360" cy="180" r="90" fill="#00d4ff" opacity="0.12" filter="url(#mfGlow)" />
  <circle cx="160" cy="340" r="100" fill="#6366f1" opacity="0.15" filter="url(#mfGlow)" />

  <!-- 3D Geometric Faceted "M" -->
  <!-- Left Pillar Face -->
  <path
    d="M100 376 V176 L164 136 V376 H100 Z"
    fill="url(#mfLeftPillar)"
  />

  <!-- Left Inward Slope (Depth shadow) -->
  <path
    d="M164 136 L248 296 L220 336 L164 230 V376 H164 Z"
    fill="url(#mfLeftInner)"
    opacity="0.9"
  />

  <!-- Center V-Peak reflection -->
  <path
    d="M164 136 L248 296 L276 244 L196 116 Z"
    fill="#818cf8"
    opacity="0.8"
  />

  <!-- Right Rising Momentum Diagonal (Center valley to peak) -->
  <path
    d="M248 296 L364 120 L404 146 L276 348 Z"
    fill="url(#mfRightSurge)"
  />

  <!-- Right Inward Slope (Depth) -->
  <path
    d="M276 348 L404 146 L380 376 H330 L310 270 Z"
    fill="url(#mfRightInner)"
  />

  <!-- Right Pillar Wing -->
  <path
    d="M344 376 V224 L408 140 V376 H344 Z"
    fill="url(#mfRightSurge)"
  />

  <!-- Upward Momentum Trend Arrow at top-right -->
  <g filter="url(#mfGlow)">
    <path
      d="M380 108 L436 104 L432 160 L408 136 L366 182 L344 160 L386 114 Z"
      fill="#38bdf8"
    />
  </g>
</svg>
```

- [ ] **Step 2: Verify `favicon.svg` valid XML formatting**

Run: `node -e "const fs = require('fs'); const content = fs.readFileSync('frontend/public/favicon.svg', 'utf8'); if (!content.includes('M100 376')) throw new Error('Invalid SVG'); console.log('SVG OK');"`
Expected: `SVG OK`

- [ ] **Step 3: Commit Task 2**

```bash
git add frontend/public/favicon.svg
git commit -m "feat(branding): 🎨 upgrade favicon.svg with 3D faceted M and momentum trendline"
```

---

### Task 3: Redesign Desktop Sidebar Logo and Menu Icons in `Layout.vue`

**Files:**
- Modify: `frontend/src/views/Layout.vue`

- [ ] **Step 1: Replace generic wallet logo with `<AppLogo>` and update 12 menu items to Phosphor Duotone**

In `frontend/src/views/Layout.vue`:
1. In `<div class="logo">`:
Replace:
```html
      <div class="logo">
        <div class="logo-icon-wrapper">
          <Icon icon="ph:wallet" class="logo-icon-phosphor" />
        </div>
        <span v-show="!isCollapsed" class="logo-text">Minefolio</span>
      </div>
```
With:
```html
      <div class="logo" @click="goTo('/dashboard')">
        <AppLogo :size="32" :with-text="!isCollapsed" />
      </div>
```
2. In `<el-menu>` items, update icons to:
- Dashboard: `<Icon icon="ph:squares-four-duotone" class="nav-icon" />`
- Assets: `<Icon icon="ph:vault-duotone" class="nav-icon" />`
- Holdings: `<Icon icon="ph:trend-up-duotone" class="nav-icon" />`
- Reports: `<Icon icon="ph:presentation-chart-duotone" class="nav-icon" />`
- Transactions: `<Icon icon="ph:arrows-left-right-duotone" class="nav-icon" />`
- Daily Expenses: `<Icon icon="ph:receipt-duotone" class="nav-icon" />`
- Plans: `<Icon icon="ph:target-duotone" class="nav-icon" />`
- Categories: `<Icon icon="ph:tree-structure-duotone" class="nav-icon" />`
- AI Chat: `<Icon icon="ph:sparkle-duotone" class="nav-icon" />`
- AI Traces: `<Icon icon="ph:activity-duotone" class="nav-icon" />`
- Audit Logs: `<Icon icon="ph:shield-check-duotone" class="nav-icon" />`
- Settings: `<Icon icon="ph:sliders-horizontal-duotone" class="nav-icon" />`

3. Import `AppLogo`:
```ts
import AppLogo from '@/components/AppLogo.vue'
```

4. In CSS styles:
Add duotone hover and active drop-shadow styling:
```css
:deep(.nav-icon) {
  font-size: 18px;
  margin-right: 12px;
  flex-shrink: 0;
  transition: transform 0.2s ease, filter 0.2s ease, color 0.2s ease;
}

:deep(.el-menu-item:hover .nav-icon) {
  transform: scale(1.1);
  color: var(--mf-primary);
}

:deep(.el-menu-item.is-active .nav-icon) {
  color: var(--mf-primary);
  filter: drop-shadow(0 0 6px var(--mf-primary-light));
}
```

- [ ] **Step 2: Run frontend unit tests to verify no regressions**

Run: `cd frontend && npm test`
Expected: PASS (All tests pass)

- [ ] **Step 3: Commit Task 3**

```bash
git add frontend/src/views/Layout.vue
git commit -m "feat(ui): ✨ integrate AppLogo and Phosphor Duotone navigation icons in desktop layout"
```

---

### Task 4: Unify Mobile Navigation Menu with Phosphor Duotone Icons in `MobileLayout.vue`

**Files:**
- Modify: `frontend/src/views-mobile/MobileLayout.vue`

- [ ] **Step 1: Replace Element Plus icons with `@iconify/vue` in `MobileLayout.vue`**

In `frontend/src/views-mobile/MobileLayout.vue`:
1. Import `Icon`:
```ts
import { Icon } from '@iconify/vue'
```
Remove unneeded `@element-plus/icons-vue` imports from `tabs`.

2. Update `tabs` definition:
```ts
const tabs = computed(() => [
  { name: 'dashboard', label: t('nav.dashboard'), icon: 'ph:squares-four-duotone', prefix: '/m/dashboard' },
  { name: 'expenses', label: t('nav.dailyExpenses'), icon: 'ph:receipt-duotone', prefix: '/m/expenses' },
  { name: 'assets', label: t('nav.assets'), icon: 'ph:vault-duotone', prefix: '/m/assets' },
  { name: 'plans', label: t('nav.plans'), icon: 'ph:target-duotone', prefix: '/m/plans' },
  { name: 'reports', label: t('nav.reports'), icon: 'ph:presentation-chart-duotone', prefix: '/m/reports' },
  { name: 'settings', label: t('nav.settings'), icon: 'ph:sliders-horizontal-duotone', prefix: '/m/settings' },
])
```

3. In template:
Replace:
```html
<el-icon :size="22"><component :is="tab.icon" /></el-icon>
```
With:
```html
<Icon :icon="tab.icon" class="tab-icon" />
```

4. In CSS:
Add styling for `.tab-icon`:
```css
.tab-icon {
  font-size: 22px;
  transition: transform 0.2s cubic-bezier(0.34, 1.56, 0.64, 1), filter 0.2s ease;
}

.tab-item.active .tab-icon {
  transform: scale(1.12);
  filter: drop-shadow(0 0 5px var(--mf-primary-light));
}
```

- [ ] **Step 2: Run mobile build to verify compilation**

Run: `cd frontend && npm run build:mobile`
Expected: Exit 0 (Mobile build succeeds)

- [ ] **Step 3: Commit Task 4**

```bash
git add frontend/src/views-mobile/MobileLayout.vue
git commit -m "feat(mobile): 📱 unify mobile bottom tabs with Phosphor Duotone iconography"
```

---

### Task 5: Integrate Brand `<AppLogo>` in `Login.vue` and `Setup.vue`

**Files:**
- Modify: `frontend/src/views/Login.vue`
- Modify: `frontend/src/views/Setup.vue`

- [ ] **Step 1: Integrate `<AppLogo>` into `Login.vue` header**

In `frontend/src/views/Login.vue`:
1. Import `AppLogo`:
```ts
import AppLogo from '@/components/AppLogo.vue'
```
2. In `<div class="card-header">`:
```html
          <div class="card-header">
            <div class="login-brand-header">
              <AppLogo :size="46" />
              <h2 class="app-title">Minefolio</h2>
            </div>
            <p class="subtitle">{{ t('login.subtitle') }}</p>
            <div class="status-line">
              <span class="status-dot"></span>
              <span class="status-text">{{ statusText }}</span>
              <span class="status-cursor"></span>
            </div>
          </div>
```
3. Add CSS for `.login-brand-header`:
```css
.login-brand-header {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 12px;
}
```

- [ ] **Step 2: Integrate `<AppLogo>` into `Setup.vue` header**

In `frontend/src/views/Setup.vue`:
1. Import `AppLogo`:
```ts
import AppLogo from '@/components/AppLogo.vue'
```
2. In `<div class="card-header">`:
```html
        <div class="card-header">
          <div class="brand-badge">
            <span class="pulse-dot"></span>
            首次部署
          </div>
          <div class="setup-brand-header">
            <AppLogo :size="42" />
            <h2 class="app-title">Minefolio 初始化</h2>
          </div>
          <p class="subtitle">欢迎使用个人资产管理系统，请设置管理员账号</p>
        </div>
```
3. Add CSS for `.setup-brand-header`:
```css
.setup-brand-header {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 12px;
}
```

- [ ] **Step 3: Run unit tests to verify login and app tests pass**

Run: `cd frontend && npm test`
Expected: Exit 0 (All unit tests pass)

- [ ] **Step 4: Commit Task 5**

```bash
git add frontend/src/views/Login.vue frontend/src/views/Setup.vue
git commit -m "feat(auth): 🎨 embed AppLogo into login and setup card headers"
```

---

### Task 6: Full Verification and Cross-Platform Build Check

**Files:**
- Test all modified files across Desktop and Mobile

- [ ] **Step 1: Run all unit tests**

Run: `cd frontend && npm test`
Expected: 100% tests pass (13 test files, 0 failures)

- [ ] **Step 2: Run desktop build and type-checking**

Run: `cd frontend && npm run build`
Expected: Exit code 0, `vue-tsc -b` passes with 0 errors

- [ ] **Step 3: Run mobile build and type-checking**

Run: `cd frontend && npm run build:mobile`
Expected: Exit code 0, `dist-mobile/index.html` produced cleanly

- [ ] **Step 4: Verify git status is clean**

Run: `git status`
Expected: working tree clean, no untracked or dirty files
