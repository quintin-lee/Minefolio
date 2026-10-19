<template>
  <div class="indices-filter-bar">
    <el-radio-group
      :model-value="marketStore.activeCategory"
      size="default"
      class="indices-radio-group"
      @update:model-value="marketStore.setActiveCategory($event as string)"
    >
      <el-radio-button value="all">{{ t('market.indicesData.categories.all') }}</el-radio-button>
      <el-radio-button value="cn">{{ t('market.indicesData.categories.cn') }}</el-radio-button>
      <el-radio-button value="us">{{ t('market.indicesData.categories.us') }}</el-radio-button>
      <el-radio-button value="hk">{{ t('market.indicesData.categories.hk') }}</el-radio-button>
      <el-radio-button value="global">{{ t('market.indicesData.categories.global') }}</el-radio-button>
      <el-radio-button value="crypto">{{ t('market.indicesData.categories.crypto') }}</el-radio-button>
    </el-radio-group>

    <div class="search-input-wrap">
      <el-input
        v-model="searchQuery"
        size="default"
        clearable
        :placeholder="t('market.indicesData.searchPlaceholder')"
        :prefix-icon="Search"
        @input="onInput"
      />
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, watch } from 'vue'
import { useI18n } from '@/composables/useI18n'
import { Search } from '@element-plus/icons-vue'
import { useMarketStore } from '@/stores/market'

const emit = defineEmits<{
  (e: 'update:filter', keyword: string): void
}>()

const { t } = useI18n()
const marketStore = useMarketStore()
const searchQuery = ref('')

function onInput(val: string) {
  emit('update:filter', val)
}

watch(searchQuery, (val) => {
  emit('update:filter', val)
})
</script>

<style scoped>
.indices-filter-bar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 14px;
  gap: 12px;
  flex-wrap: wrap;
}
.indices-radio-group :deep(.el-radio-button__inner) {
  background: var(--mf-surface-muted) !important;
  border: 1px solid transparent !important;
  box-shadow: none !important;
  color: var(--mf-text-muted) !important;
  border-radius: var(--mf-radius-md) !important;
  margin-right: 6px;
  transition: var(--mf-transition);
}
.indices-radio-group :deep(.el-radio-button:first-child .el-radio-button__inner) {
  border-left: 1px solid transparent !important;
}
.indices-radio-group :deep(.el-radio-button:hover .el-radio-button__inner) {
  color: var(--mf-primary) !important;
  background: var(--mf-surface-hover) !important;
}
.indices-radio-group :deep(.el-radio-button.is-active .el-radio-button__inner) {
  background: var(--mf-primary) !important;
  color: #ffffff !important;
  box-shadow: 0 0 10px rgba(59, 130, 246, 0.3) !important;
}
.search-input-wrap {
  width: 240px;
}
.search-input-wrap :deep(.el-input__wrapper) {
  background: var(--mf-surface-muted);
  box-shadow: 0 0 0 1px var(--mf-border-subtle) inset;
  border-radius: var(--mf-radius-md);
}
.search-input-wrap :deep(.el-input__wrapper.is-focus) {
  box-shadow: 0 0 0 1px var(--mf-primary) inset;
}
</style>
