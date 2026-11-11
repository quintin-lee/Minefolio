<template>
  <div class="mobile-layout">
    <header class="mobile-top-bar">
      <LedgerSelector class="mobile-ledger" />
      <LocaleToggle class="locale-chip" />
    </header>
    <main class="mobile-content">
      <router-view :key="routerViewKey" />
    </main>
    <nav class="tab-bar">
      <button
        v-for="tab in tabs"
        :key="tab.name"
        class="tab-item"
        :class="{ active: route.path.startsWith(tab.prefix) }"
        @click="go(tab)"
      >
        <Icon :icon="tab.icon" class="tab-icon" />
        <span>{{ tab.label }}</span>
      </button>
    </nav>
  </div>
</template>

<script setup lang="ts">
import { computed, ref, onMounted, onBeforeUnmount } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { Icon } from '@iconify/vue'
import LocaleToggle from '@/components/LocaleToggle.vue'
import LedgerSelector from '@/components/LedgerSelector.vue'
import { useCategoryStore } from '@/stores/category'
import { useSyncStore } from '@/stores/sync'
import { t } from '@/utils/locale'

const route = useRoute()
const router = useRouter()
const categoryStore = useCategoryStore()
const syncStore = useSyncStore()
const routerViewKey = ref(0)

function handleLedgerChanged() {
  categoryStore.invalidate()
  syncStore.syncNow()
  routerViewKey.value++
}

onMounted(() => {
  window.addEventListener('minefolio:ledger-changed', handleLedgerChanged)
  // 进入主界面后立即做一次双向同步，确保本地离线库尽快与远端对齐
  // (而非等到网络/前后台事件或用户手动点击才同步)
  void syncStore.syncNow()
})

onBeforeUnmount(() => {
  window.removeEventListener('minefolio:ledger-changed', handleLedgerChanged)
})

const tabs = computed(() => [
  { name: 'dashboard', label: t('nav.dashboard'), icon: 'ph:squares-four-duotone', prefix: '/m/dashboard' },
  { name: 'expenses', label: t('nav.dailyExpenses'), icon: 'ph:receipt-duotone', prefix: '/m/expenses' },
  { name: 'assets', label: t('nav.assets'), icon: 'ph:vault-duotone', prefix: '/m/assets' },
  { name: 'plans', label: t('nav.plans'), icon: 'ph:target-duotone', prefix: '/m/plans' },
  { name: 'reports', label: t('nav.reports'), icon: 'ph:presentation-chart-duotone', prefix: '/m/reports' },
  { name: 'settings', label: t('nav.settings'), icon: 'ph:sliders-horizontal-duotone', prefix: '/m/settings' },
])

function go(tab: { prefix: string }) {
  router.push(tab.prefix)
}
</script>

<style scoped>
.mobile-layout {
  display: flex;
  flex-direction: column;
  height: 100vh;
  height: 100dvh;
  background: var(--mf-background);
}
.mobile-top-bar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 8px 14px;
  padding-top: calc(8px + env(safe-area-inset-top));
  padding-left: calc(14px + env(safe-area-inset-left));
  padding-right: calc(14px + env(safe-area-inset-right));
  background: var(--mf-surface);
  border-bottom: 1px solid var(--mf-border);
  flex-shrink: 0;
  z-index: 20;
}
.mobile-ledger :deep(.ledger-selector-wrap) {
  margin-right: 0;
}
.mobile-ledger :deep(.current-ledger-btn) {
  padding: 4px 8px;
  max-width: 210px;
}
.mobile-ledger :deep(.ledger-name) {
  max-width: 100px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  font-size: 13px;
}
.locale-chip {
  flex-shrink: 0;
}
.mobile-content {
  flex: 1;
  overflow-y: auto;
  padding: 14px;
  padding-left: calc(14px + env(safe-area-inset-left));
  padding-right: calc(14px + env(safe-area-inset-right));
  padding-bottom: calc(76px + env(safe-area-inset-bottom));
}
.tab-bar {
  position: fixed;
  bottom: 0;
  left: 0;
  right: 0;
  height: calc(56px + env(safe-area-inset-bottom));
  display: flex;
  background: var(--mf-surface);
  border-top: 1px solid var(--mf-border);
  padding-bottom: env(safe-area-inset-bottom);
  padding-left: env(safe-area-inset-left);
  padding-right: env(safe-area-inset-right);
  box-sizing: border-box;
}
.tab-item {
  flex: 1;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 2px;
  background: none;
  border: none;
  color: var(--mf-text-muted);
  font-size: 12px;
  cursor: pointer;
  transition: color 0.2s ease;
}
.tab-icon {
  font-size: 22px;
  transition: transform 0.2s cubic-bezier(0.34, 1.56, 0.64, 1), filter 0.2s ease;
}
.tab-item.active {
  color: var(--mf-primary);
}
.tab-item.active .tab-icon {
  transform: scale(1.12);
  filter: drop-shadow(0 0 5px var(--mf-primary-light));
}
</style>
