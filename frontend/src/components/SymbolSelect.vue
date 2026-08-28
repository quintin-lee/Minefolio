<template>
  <div class="symbol-select">
    <el-select
      v-model="innerValue"
      filterable
      remote
      reserve-keyword
      clearable
      placeholder="输入代码或名称 (如 110011, 茅台, AAPL, BTC)"
      :remote-method="searchSymbols"
      :loading="loading"
      style="width: 100%"
      @change="handleChange"
    >
      <el-option
        v-for="item in options"
        :key="item.symbol"
        :label="`${item.symbol} - ${item.name}`"
        :value="item.symbol"
      >
        <div class="option-item">
          <div class="option-main">
            <span class="option-symbol">{{ item.symbol }}</span>
            <span class="option-name">{{ item.name }}</span>
          </div>
          <div class="option-meta">
            <el-tag size="small" :type="getSourceTagType(item.source)" effect="light">
              {{ item.market_desc || item.source }}
            </el-tag>
          </div>
        </div>
      </el-option>
    </el-select>
  </div>
</template>

<script setup lang="ts">
import { ref, watch } from 'vue'
import { marketApi } from '@/api/market'
import type { MarketSearchItem } from '@/types'

const props = defineProps<{
  modelValue?: string
}>()

const emit = defineEmits<{
  (e: 'update:modelValue', value: string): void
  (e: 'select', item: MarketSearchItem): void
}>()

const innerValue = ref(props.modelValue || '')
const options = ref<MarketSearchItem[]>([])
const loading = ref(false)
let debounceTimer: any = null

watch(() => props.modelValue, (v) => {
  innerValue.value = v || ''
})

function getSourceTagType(source: string): 'primary' | 'success' | 'warning' | 'info' | 'danger' {
  if (source.includes('fund')) return 'warning'
  if (source.includes('crypto')) return 'success'
  if (source.includes('us') || source.includes('hk')) return 'info'
  return 'primary'
}

function searchSymbols(query: string) {
  if (!query || query.trim().length === 0) {
    options.value = []
    return
  }

  if (debounceTimer) clearTimeout(debounceTimer)
  debounceTimer = setTimeout(async () => {
    loading.value = true
    try {
      const res = await marketApi.search(query.trim())
      options.value = res || []
    } catch (e) {
      console.error('[SymbolSelect] Search failed:', e)
      options.value = []
    } finally {
      loading.value = false
    }
  }, 300)
}

function handleChange(val: string) {
  emit('update:modelValue', val)
  const item = options.value.find(o => o.symbol === val)
  if (item) {
    emit('select', item)
  }
}
</script>

<style scoped>
.symbol-select {
  width: 100%;
}
.option-item {
  display: flex;
  justify-content: space-between;
  align-items: center;
  width: 100%;
}
.option-main {
  display: flex;
  gap: 8px;
  align-items: center;
}
.option-symbol {
  font-weight: 600;
  color: var(--el-color-primary);
  font-family: monospace;
}
.option-name {
  color: var(--el-text-color-regular);
  max-width: 180px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.option-meta {
  margin-left: 8px;
}
</style>
