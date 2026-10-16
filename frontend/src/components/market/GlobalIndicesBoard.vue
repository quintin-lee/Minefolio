<template>
  <div class="global-indices-board">
    <!-- 1. Market Clocks Ribbon -->
    <MarketClocksRibbon />

    <!-- 2. Categories & Search Filter -->
    <IndicesFilterBar @update:filter="keywordFilter = $event" />

    <!-- 3. Indices Responsive Grid -->
    <div v-loading="marketStore.isLoadingIndices" class="indices-grid">
      <IndexCard
        v-for="item in filteredIndices"
        :key="item.symbol"
        :item="item"
        :direction="marketStore.priceDirections[item.symbol]"
        @select="marketStore.openIndexDrawer($event)"
      />
    </div>

    <el-empty
      v-if="!marketStore.isLoadingIndices && filteredIndices.length === 0"
      description="未找到匹配的大盘指数"
    />

    <!-- 4. Interactive Detail Drawer -->
    <IndexDetailDrawer />
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch, onMounted, onUnmounted } from 'vue'
import { useMarketStore } from '@/stores/market'
import MarketClocksRibbon from '@/components/market/MarketClocksRibbon.vue'
import IndicesFilterBar from '@/components/market/IndicesFilterBar.vue'
import IndexCard from '@/components/market/IndexCard.vue'
import IndexDetailDrawer from '@/components/market/IndexDetailDrawer.vue'

const marketStore = useMarketStore()
const keywordFilter = ref('')
let timerId: ReturnType<typeof setInterval> | null = null

const filteredIndices = computed(() => {
  const kw = keywordFilter.value.trim().toLowerCase()
  if (!kw) return marketStore.indices
  return marketStore.indices.filter(
    (item) => item.name.toLowerCase().includes(kw) || item.symbol.toLowerCase().includes(kw)
  )
})

const effectiveIntervalSec = computed(() => {
  if (marketStore.autoRefreshInterval <= 0) {
    return 0
  }
  // Adaptive sleep: if all equity markets are closed, poll at 15m (900s) sleep cadence
  if (!marketStore.isAnyMarketActive) {
    return 900
  }
  return marketStore.autoRefreshInterval
})

function startTimer() {
  stopTimer()
  const intervalSec = effectiveIntervalSec.value
  if (intervalSec > 0) {
    timerId = setInterval(() => {
      marketStore.fetchIndices(undefined, true)
    }, intervalSec * 1000)
  }
}

function stopTimer() {
  if (timerId) {
    clearInterval(timerId)
    timerId = null
  }
}

watch(effectiveIntervalSec, () => {
  startTimer()
})

function handleVisibilityChange() {
  if (document.hidden) {
    stopTimer()
  } else {
    marketStore.fetchIndices(undefined, true)
    startTimer()
  }
}

onMounted(() => {
  marketStore.fetchIndices()
  startTimer()
  document.addEventListener('visibilitychange', handleVisibilityChange)
})

onUnmounted(() => {
  stopTimer()
  document.removeEventListener('visibilitychange', handleVisibilityChange)
})
</script>

<style scoped>
.global-indices-board {
  display: flex;
  flex-direction: column;
}
.indices-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));
  gap: 14px;
}
</style>
