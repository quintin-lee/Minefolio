<template>
  <div class="settings-page">
    <PageHeader :title="t('nav.settings')" class="settings-page-header" />

    <el-tabs v-model="activeTab" class="settings-tabs" :tab-position="tabPosition" style="--el-tabs-ink-height: 3px;">
      <el-tab-pane name="profile" lazy>
        <template #label>
          <Icon icon="ph:user-circle" width="16" />
          <span class="tab-label">{{ t('settings.tabAccount') }}</span>
        </template>
        <UserHeroCard />
      </el-tab-pane>

      <el-tab-pane name="password" lazy>
        <template #label>
          <Icon icon="ph:lock-key" width="16" />
          <span class="tab-label">{{ t('settings.changePassword') }}</span>
        </template>
        <PasswordSettings />
      </el-tab-pane>

      <el-tab-pane name="2fa" lazy>
        <template #label>
          <Icon icon="ph:shield-check" width="16" />
          <span class="tab-label">{{ t('settings.tabTwoFactor') }}</span>
        </template>
        <TwoFactorSettings />
      </el-tab-pane>

      <el-tab-pane name="export" lazy>
        <template #label>
          <Icon icon="ph:export" width="16" />
          <span class="tab-label">{{ t('settings.exportData') }}</span>
        </template>
        <DataExport />
      </el-tab-pane>

      <el-tab-pane name="import-rules" lazy>
        <template #label>
          <Icon icon="ph:lightning" width="16" />
          <span class="tab-label">{{ t('settings.tabImportRules') }}</span>
        </template>
        <ImportRulesManager />
      </el-tab-pane>

      <el-tab-pane name="ai" lazy>
        <template #label>
          <Icon icon="ph:brain" width="16" />
          <span class="tab-label">{{ t('settings.tabAi') }}</span>
        </template>
        <AiProviderManager />
      </el-tab-pane>

      <el-tab-pane name="market" lazy>
        <template #label>
          <Icon icon="ph:chart-line" width="16" />
          <span class="tab-label">{{ t('settings.tabMarket') }}</span>
        </template>
        <MarketSyncSettings />
      </el-tab-pane>

      <el-tab-pane name="backup" lazy>
        <template #label>
          <Icon icon="ph:database" width="16" />
          <span class="tab-label">{{ t('settings.tabBackup') }}</span>
        </template>
        <DatabaseBackup />
      </el-tab-pane>

      <el-tab-pane name="im-bot" lazy>
        <template #label>
          <Icon icon="ph:robot" width="16" />
          <span class="tab-label">{{ t('settings.tabImBot') }}</span>
        </template>
        <ImBotSettings />
      </el-tab-pane>

      <el-tab-pane v-if="auth.isAdmin" name="users" lazy>
        <template #label>
          <Icon icon="ph:users" width="16" />
          <span class="tab-label">{{ t('settings.tabUserManagement') }}</span>
        </template>
        <UserManagement />
      </el-tab-pane>

      <el-tab-pane name="appearance" lazy>
        <template #label>
          <Icon icon="ph:palette" width="16" />
          <span class="tab-label">{{ t('settings.theme') }}</span>
        </template>
        <AppearanceSettings />
      </el-tab-pane>
    </el-tabs>
  </div>
</template>

<script setup lang="ts">
import { ref, onErrorCaptured, onMounted, onBeforeUnmount } from 'vue'
import { useRoute } from 'vue-router'
import { ElMessage } from 'element-plus'
import { Icon } from '@iconify/vue'
import { useAuthStore } from '@/stores/auth'
import { t } from '@/utils/locale'
import PageHeader from '@/components/PageHeader.vue'
import UserHeroCard from '@/components/settings/UserHeroCard.vue'
import PasswordSettings from '@/components/settings/PasswordSettings.vue'
import TwoFactorSettings from '@/components/settings/TwoFactorSettings.vue'
import DataExport from '@/components/settings/DataExport.vue'
import ImportRulesManager from '@/components/settings/ImportRulesManager.vue'
import AiProviderManager from '@/components/settings/AiProviderManager.vue'
import MarketSyncSettings from '@/components/settings/MarketSyncSettings.vue'
import DatabaseBackup from '@/components/settings/DatabaseBackup.vue'
import ImBotSettings from '@/components/settings/ImBotSettings.vue'
import UserManagement from '@/components/settings/UserManagement.vue'
import AppearanceSettings from '@/components/settings/AppearanceSettings.vue'

const auth = useAuthStore()
const route = useRoute()
const ALLOWED_TABS = new Set([
  'profile',
  'password',
  '2fa',
  'export',
  'import-rules',
  'ai',
  'market',
  'backup',
  'im-bot',
  'users',
  'appearance',
])
const rawTab = typeof route?.query?.tab === 'string' ? route.query.tab : ''
const initialTab = ALLOWED_TABS.has(rawTab) && (rawTab !== 'users' || auth.isAdmin) ? rawTab : 'profile'
const activeTab = ref(initialTab)
const tabPosition = ref<'left' | 'top'>(
  typeof window !== 'undefined' && window.innerWidth < 900 ? 'top' : 'left'
)

function handleResize() {
  if (typeof window !== 'undefined') {
    tabPosition.value = window.innerWidth < 900 ? 'top' : 'left'
  }
}

onMounted(() => {
  handleResize()
  if (typeof window !== 'undefined') {
    window.addEventListener('resize', handleResize)
  }
})

onBeforeUnmount(() => {
  if (typeof window !== 'undefined') {
    window.removeEventListener('resize', handleResize)
  }
})

onErrorCaptured((err, instance, info) => {
  console.error('[Settings] Child component error:', err, info)
  ElMessage.error(t('settings.pageLoadError'))
  return false
})
</script>

<style scoped>
.settings-page {
  background-color: var(--mf-background);
  height: 100%;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.settings-page-header {
  padding: 0 0 16px 0;
  border-bottom: 1px solid var(--mf-border);
  background: var(--mf-background);
}

.settings-tabs {
  flex: 1;
  min-height: 0;
  overflow: hidden;
  padding: 0;
  display: flex;
}

.settings-tabs.el-tabs--top {
  flex-direction: column;
}

.settings-tabs.el-tabs--left :deep(.el-tabs__header.is-left) {
  margin-right: 24px;
  flex-shrink: 0;
  min-width: 176px;
  border-right: 1px solid var(--mf-border);
  padding-right: 8px;
  overflow-y: auto;
}

.settings-tabs.el-tabs--left :deep(.el-tabs__item.is-left) {
  justify-content: flex-start;
  text-align: left;
  padding: 10px 14px;
  border-radius: var(--mf-radius-md);
  height: auto;
}

.settings-tabs :deep(.el-tabs__nav) {
  border-bottom: 1px solid var(--mf-border);
  margin-bottom: 0;
}

.settings-tabs :deep(.el-tabs__nav-wrap) {
  --el-tabs-nav-wrap-item-margin: 0 16px 0 0;
}

.settings-tabs :deep(.el-tabs__item) {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 13px;
  padding: 12px 4px;
  color: var(--mf-text-regular);
}

.settings-tabs :deep(.el-tabs__item.is-active) {
  color: var(--mf-primary);
  font-weight: 600;
}

.settings-tabs :deep(.el-tabs__item:hover) {
  color: var(--mf-text-main);
}

.settings-tabs :deep(.el-tabs__content) {
  flex: 1;
  min-width: 0;
  min-height: 0;
  height: 100%;
  overflow: hidden;
  padding: 0;
}

.settings-tabs :deep(.el-tab-pane) {
  height: 100%;
  overflow-y: auto;
  padding: 20px 4px 24px 0;
  box-sizing: border-box;
}

.tab-label {
  font-size: 13px;
}

@media (max-width: 768px) {
  .settings-page {
    height: auto;
    overflow: visible;
  }

  .settings-tabs {
    overflow: visible;
  }

  .settings-tabs :deep(.el-tabs__content) {
    height: auto;
    overflow: visible;
  }

  .settings-tabs :deep(.el-tab-pane) {
    height: auto;
    overflow-y: visible;
    padding: 16px 0;
  }
}
</style>
