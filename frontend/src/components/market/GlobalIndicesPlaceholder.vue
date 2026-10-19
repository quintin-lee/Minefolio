<template>
  <div class="indices-placeholder-container">
    <el-card shadow="never" class="placeholder-card">
      <div class="placeholder-content">
        <div class="badge-ribbon">
          <el-tag type="warning" effect="dark" round>
            🔮 {{ t('market.indicesPlaceholder.badge') }}
          </el-tag>
        </div>

        <h2 class="placeholder-title">
          {{ t('market.indicesPlaceholder.title') }}
        </h2>
        <p class="placeholder-desc">
          {{ t('market.indicesPlaceholder.desc') }}
        </p>

        <!-- Preview Mock Grid of Global Indices -->
        <div class="mock-indices-grid">
          <div class="mock-index-box">
            <span class="index-name">标普 500 (S&P 500)</span>
            <span class="index-val">5,860.20</span>
            <span class="index-up">+0.42%</span>
          </div>
          <div class="mock-index-box">
            <span class="index-name">纳斯达克 100 (NDX)</span>
            <span class="index-val">20,380.50</span>
            <span class="index-up">+0.68%</span>
          </div>
          <div class="mock-index-box">
            <span class="index-name">上证指数 (000001)</span>
            <span class="index-val">3,280.10</span>
            <span class="index-down">-0.15%</span>
          </div>
          <div class="mock-index-box">
            <span class="index-name">恒生指数 (HSI)</span>
            <span class="index-val">20,550.00</span>
            <span class="index-up">+0.85%</span>
          </div>
        </div>
      </div>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { useI18n } from 'vue-i18n'

const { t } = useI18n()
</script>

<style scoped>
.indices-placeholder-container {
  padding: 20px 0;
}

.placeholder-card {
  border-radius: var(--mf-radius-lg);
  text-align: center;
  padding: 40px 20px;
  background: var(--mf-surface-card);
  border: 1px dashed var(--mf-border-subtle);
}

.placeholder-content {
  max-width: 680px;
  margin: 0 auto;
}

.badge-ribbon {
  margin-bottom: 16px;
}

.placeholder-title {
  font-size: 22px;
  font-weight: 700;
  color: var(--el-text-color-primary);
  margin-bottom: 12px;
}

.placeholder-desc {
  font-size: 14px;
  color: var(--el-text-color-secondary);
  line-height: 1.6;
  margin-bottom: 30px;
}

.mock-indices-grid {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 12px;
  opacity: 0.85;
}

.mock-index-box {
  background: var(--mf-surface-muted);
  border: 1px solid var(--mf-border-subtle);
  border-radius: var(--mf-radius-sm);
  padding: 12px 8px;
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.index-name {
  font-size: 11px;
  color: var(--el-text-color-secondary);
}

.index-val {
  font-size: 16px;
  font-weight: 700;
  color: var(--el-text-color-primary);
}

.index-up {
  font-size: 11px;
  font-weight: 600;
  color: var(--el-color-success);
}

.index-down {
  font-size: 11px;
  font-weight: 600;
  color: var(--el-color-danger);
}
</style>
