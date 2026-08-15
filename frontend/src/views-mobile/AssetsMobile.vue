<template>
  <div class="assets-mobile">
    <div class="page-header"><h2>资产</h2></div>
    <div v-for="a in list" :key="a.id" class="asset-card">
      <span class="name">{{ a.name }}</span>
      <span class="value">{{ fmt(a.current_value) }}</span>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { assetsApi } from '@/api/assets'
import type { Asset } from '@/types'

const list = ref<Asset[]>([])
function fmt(v: number) { return new Intl.NumberFormat('zh-CN', { style: 'currency', currency: 'CNY' }).format(v ?? 0) }
onMounted(async () => {
  const res = await assetsApi.list({ page_size: 500 })
  list.value = res.list
})
</script>

<style scoped>
.asset-card { display: flex; justify-content: space-between; background: var(--mf-surface); border: 1px solid var(--mf-border); border-radius: 12px; padding: 14px; margin-bottom: 10px; }
.asset-card .value { font-family: 'JetBrains Mono', monospace; }
</style>
