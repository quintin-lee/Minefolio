<template>
  <div class="indices-filter-bar">
    <el-radio-group
      :model-value="marketStore.activeCategory"
      size="default"
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
.search-input-wrap {
  width: 240px;
}
</style>
