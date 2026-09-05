<template>
  <div class="settings-page">
    <div class="page-header">
      <div class="header-title">
        <div class="title-accent"></div>
        <h2>{{ t('settings.title') }}</h2>
      </div>
    </div>

    <el-tabs v-model="activeTab" class="settings-tabs" :tab-position="'top'" style="--el-tabs-ink-height: 3px;">
      <el-tab-pane name="profile" lazy>
        <template #label>
          <Icon icon="ph:user-circle" width="16" />
          <span class="tab-label">账号信息</span>
        </template>
        <UserHeroCard />
      </el-tab-pane>

      <el-tab-pane name="password" lazy>
        <template #label>
          <Icon icon="ph:lock-key" width="16" />
          <span class="tab-label">修改密码</span>
        </template>
        <PasswordSettings />
      </el-tab-pane>

      <el-tab-pane name="2fa" lazy>
        <template #label>
          <Icon icon="ph:shield-check" width="16" />
          <span class="tab-label">两步验证</span>
        </template>
        <TwoFactorSettings />
      </el-tab-pane>

      <el-tab-pane name="export" lazy>
        <template #label>
          <Icon icon="ph:export" width="16" />
          <span class="tab-label">数据导出</span>
        </template>
        <DataExport />
      </el-tab-pane>

      <el-tab-pane name="import-rules" lazy>
        <template #label>
          <Icon icon="ph:lightning" width="16" />
          <span class="tab-label">导入规则</span>
        </template>
        <ImportRulesManager />
      </el-tab-pane>

      <el-tab-pane name="ai" lazy>
        <template #label>
          <Icon icon="ph:brain" width="16" />
          <span class="tab-label">AI 配置</span>
        </template>
        <AiProviderManager />
      </el-tab-pane>

      <el-tab-pane name="market" lazy>
        <template #label>
          <Icon icon="ph:chart-line" width="16" />
          <span class="tab-label">行情同步</span>
        </template>
        <MarketSyncSettings />
      </el-tab-pane>

      <el-tab-pane name="appearance" lazy>
        <template #label>
          <Icon icon="ph:palette" width="16" />
          <span class="tab-label">外观主题</span>
        </template>
        <AppearanceSettings />
      </el-tab-pane>
    </el-tabs>
  </div>
</template>

<script setup lang="ts">
import { ref, onErrorCaptured } from 'vue'
import { ElMessage } from 'element-plus'
import { Icon } from '@iconify/vue'
import { t } from '@/utils/locale'
import UserHeroCard from '@/components/settings/UserHeroCard.vue'
import PasswordSettings from '@/components/settings/PasswordSettings.vue'
import TwoFactorSettings from '@/components/settings/TwoFactorSettings.vue'
import DataExport from '@/components/settings/DataExport.vue'
import ImportRulesManager from '@/components/settings/ImportRulesManager.vue'
import AiProviderManager from '@/components/settings/AiProviderManager.vue'
import MarketSyncSettings from '@/components/settings/MarketSyncSettings.vue'
import AppearanceSettings from '@/components/settings/AppearanceSettings.vue'

const activeTab = ref('profile')

onErrorCaptured((err, instance, info) => {
  console.error('[Settings] Child component error:', err, info)
  ElMessage.error('页面加载异常，请刷新重试')
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

.page-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 16px 24px;
  border-bottom: 1px solid var(--border-color);
  background: var(--bg-primary);
  flex-shrink: 0;
}

.header-title {
  display: flex;
  align-items: center;
  gap: 12px;
}

.title-accent {
  width: 4px;
  height: 24px;
  background: linear-gradient(180deg, #6366f1, #8b5cf6);
  border-radius: 2px;
}

.header-title h2 {
  margin: 0;
  font-size: 18px;
  font-weight: 600;
  color: var(--text-primary);
}

.settings-tabs {
  flex: 1;
  overflow: hidden;
  padding: 0 24px;
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
  color: var(--mf-text-secondary);
}

.settings-tabs :deep(.el-tabs__item.is-active) {
  color: var(--mf-primary);
  font-weight: 600;
}

.settings-tabs :deep(.el-tabs__item:hover) {
  color: var(--mf-text-main);
}

.settings-tabs :deep(.el-tabs__content) {
  padding: 24px 0;
}

.settings-tabs :deep(.el-tab-pane) {
  height: calc(100% - 48px);
}

.tab-label {
  font-size: 13px;
}

.panel-container {
  background: var(--mf-surface);
  border-radius: var(--mf-radius-lg);
  box-shadow: var(--mf-shadow-sm);
  border: 1px solid var(--mf-border);
  padding: 24px;
}

.panel-header {
  margin-bottom: 20px;
}

.panel-header h3 {
  margin: 0;
  font-size: 16px;
  font-weight: 600;
  color: var(--mf-text-main);
}

.header-actions {
  display: flex;
  align-items: center;
  gap: 8px;
}

.export-hint {
  font-size: 13px;
  color: var(--mf-text-muted);
  margin-bottom: 16px;
  line-height: 1.6;
}

.premium-form .el-form-item {
  margin-bottom: 24px;
}

.premium-form :deep(.el-input__wrapper) {
  background-color: rgba(15, 23, 42, 0.6) !important;
  box-shadow: 0 0 0 1px var(--mf-border) inset !important;
  border-radius: var(--mf-radius-md);
  padding: 6px 12px;
}

.action-btn {
  width: 160px;
}

:deep(.el-divider) {
  margin: 24px 0;
}
</style>
