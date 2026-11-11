<template>
  <el-drawer
    v-model="marketStore.isDrawerOpen"
    :title="drawerTitle"
    size="520px"
    destroy-on-close
    class="index-detail-drawer"
  >
    <div v-if="item" class="drawer-content">
      <!-- Top Overview Stat Box -->
      <div class="overview-box" :class="isPositive ? 'trend-up' : 'trend-down'">
        <div class="overview-main">
          <div class="current-price">{{ formatNumber(item.current_price) }}</div>
          <div class="price-delta">
            <span>{{ isPositive ? '+' : '' }}{{ formatChange(item.change_amount) }}</span>
            <span>({{ isPositive ? '+' : '' }}{{ formatChange(item.change_percent) }}%)</span>
          </div>
        </div>
        <div class="quote-time-tag">
          {{ t('market.indicesData.quoteTime') }}: {{ item.quote_time }}
        </div>
      </div>

      <!-- Key Metrics Grid -->
      <div class="metrics-grid">
        <div class="metric-item">
          <span class="m-label">{{ t('market.indicesData.open') }}</span>
          <span class="m-val">{{ formatNumber(item.open_price) }}</span>
        </div>
        <div class="metric-item">
          <span class="m-label">{{ t('market.indicesData.prevClose') }}</span>
          <span class="m-val">{{ formatNumber(item.prev_close) }}</span>
        </div>
        <div class="metric-item">
          <span class="m-label">{{ t('market.indicesData.high') }}</span>
          <span class="m-val">{{ formatNumber(item.high_price) }}</span>
        </div>
        <div class="metric-item">
          <span class="m-label">{{ t('market.indicesData.low') }}</span>
          <span class="m-val">{{ formatNumber(item.low_price) }}</span>
        </div>
      </div>

      <!-- Interactive ECharts Trend Chart -->
      <div class="chart-section">
        <div class="section-title">📈 {{ t('market.indicesData.drawerTitle') }}</div>
        <div ref="chartRef" class="chart-container" />
      </div>
    </div>
  </el-drawer>
</template>

<script setup lang="ts">
import { computed, ref, watch, nextTick, onBeforeUnmount } from 'vue'
import { useI18n } from '@/composables/useI18n'
import echarts, { type ECharts, type EChartsOption } from '@/utils/echarts'
import { useMarketStore } from '@/stores/market'
import { resolveChartPalette, useChartThemeSync, withAlpha } from '@/utils/echarts-theme'

const { t } = useI18n()
const marketStore = useMarketStore()
const chartRef = ref<HTMLDivElement | null>(null)
let chartInstance: ECharts | null = null

const item = computed(() => marketStore.selectedDrawerIndex)
const isPositive = computed(() => (Number(item.value?.change_percent) || 0) >= 0)
const drawerTitle = computed(() =>
  item.value ? `${item.value.region_flag} ${item.value.name} (${item.value.symbol})` : ''
)

function formatNumber(val: number | string | undefined | null) {
  if (val === undefined || val === null || val === '') return '0.00'
  const n = Number(val)
  if (isNaN(n)) return '0.00'
  return n.toLocaleString(undefined, { minimumFractionDigits: 2, maximumFractionDigits: 2 })
}

function formatChange(val: number | string | undefined | null) {
  if (val === undefined || val === null || val === '') return '0.00'
  const n = Number(val)
  if (isNaN(n)) return '0.00'
  return n.toFixed(2)
}

function renderChart() {
  if (!chartRef.value || !item.value) return
  if (!chartInstance) {
    chartInstance = echarts.init(chartRef.value)
  }

  const palette = resolveChartPalette()
  const pts = item.value.sparkline || []
  const times = pts.map((_, i) => `${9 + Math.floor(i / 2)}:${i % 2 === 0 ? '30' : '00'}`)
  const color = isPositive.value ? palette.success : palette.danger

  const option: EChartsOption = {
    tooltip: {
      trigger: 'axis',
      formatter: '{b}<br/>点位: <b>{c}</b>',
    },
    grid: { left: '10%', right: '5%', top: '10%', bottom: '15%' },
    xAxis: {
      type: 'category',
      data: times,
      axisLine: { lineStyle: { color: palette.borderSubtle } },
      axisLabel: { color: palette.textMuted },
    },
    yAxis: {
      type: 'value',
      scale: true,
      splitLine: { lineStyle: { type: 'dashed', color: palette.isLight ? 'rgba(0, 0, 0, 0.06)' : 'rgba(255, 255, 255, 0.06)' } },
      axisLabel: { color: palette.textMuted },
    },
    series: [
      {
        name: '点位',
        type: 'line',
        data: pts,
        smooth: true,
        showSymbol: false,
        lineStyle: { width: 2.5, color },
        areaStyle: {
          color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
            { offset: 0, color: isPositive.value ? withAlpha(palette.success, 0.35) : withAlpha(palette.danger, 0.35) },
            { offset: 1, color: 'rgba(0,0,0,0)' },
          ]),
        },
      },
    ],
  }

  chartInstance.setOption(option)
}

function handleResize() {
  chartInstance?.resize()
}

useChartThemeSync(() => {
  if (marketStore.isDrawerOpen) {
    renderChart()
  }
})

watch(
  () => marketStore.isDrawerOpen,
  (open) => {
    if (open) {
      window.addEventListener('resize', handleResize)
      nextTick(() => {
        renderChart()
      })
    } else {
      window.removeEventListener('resize', handleResize)
    }
  }
)

onBeforeUnmount(() => {
  window.removeEventListener('resize', handleResize)
  if (chartInstance) {
    chartInstance.dispose()
    chartInstance = null
  }
})
</script>

<style scoped>
.drawer-content {
  display: flex;
  flex-direction: column;
  gap: 16px;
}
.overview-box {
  background: var(--mf-surface-muted);
  border-radius: var(--mf-radius-md);
  padding: 16px;
  display: flex;
  justify-content: space-between;
  align-items: flex-end;
}
.current-price {
  font-size: 28px;
  font-weight: 700;
  color: var(--el-text-color-primary);
}
.price-delta {
  font-size: 14px;
  font-weight: 600;
  margin-top: 4px;
}
.trend-up .price-delta { color: var(--el-color-success); }
.trend-down .price-delta { color: var(--el-color-danger); }
.quote-time-tag {
  font-size: 11px;
  color: var(--el-text-color-secondary);
}
.metrics-grid {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 10px;
}
.metric-item {
  background: var(--mf-surface-muted);
  border: 1px solid var(--mf-border-subtle);
  border-radius: var(--mf-radius-sm);
  padding: 10px 12px;
  display: flex;
  justify-content: space-between;
  align-items: center;
}
.m-label {
  font-size: 12px;
  color: var(--el-text-color-secondary);
}
.m-val {
  font-size: 14px;
  font-weight: 600;
  color: var(--el-text-color-primary);
}
.chart-section {
  margin-top: 8px;
}
.section-title {
  font-size: 14px;
  font-weight: 600;
  margin-bottom: 8px;
  color: var(--el-text-color-primary);
}
.chart-container {
  width: 100%;
  height: 280px;
}
</style>
