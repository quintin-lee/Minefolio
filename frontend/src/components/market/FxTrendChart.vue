<template>
  <el-card shadow="never" class="trend-card" v-loading="marketStore.isLoadingHistory">
    <template #header>
      <div class="card-header">
        <div class="header-left">
          <span class="header-title">📈 {{ t('market.chart.title') }}</span>
          <el-select
            :model-value="marketStore.selectedCurrency"
            class="currency-pair-select"
            @update:model-value="marketStore.setSelectedCurrency($event)"
          >
            <el-option
              v-for="cur in currencyOptions"
              :key="cur"
              :label="`${cur} / ${marketStore.baseCurrency}`"
              :value="cur"
            />
          </el-select>
        </div>

        <div class="header-periods">
          <el-radio-group
            :model-value="marketStore.historyDays"
            size="small"
            @update:model-value="marketStore.setHistoryDays(Number($event))"
          >
            <el-radio-button :value="7">{{ t('market.chart.periods.d7') }}</el-radio-button>
            <el-radio-button :value="30">{{ t('market.chart.periods.d30') }}</el-radio-button>
            <el-radio-button :value="90">{{ t('market.chart.periods.d90') }}</el-radio-button>
            <el-radio-button :value="365">{{ t('market.chart.periods.y1') }}</el-radio-button>
          </el-radio-group>

          <el-dropdown trigger="click" @command="handleExport" class="chart-export-dropdown">
            <el-button size="small" class="chart-action-btn" title="导出图表">
              <Icon icon="ph:download-simple-bold" class="action-icon" />
            </el-button>
            <template #dropdown>
              <el-dropdown-menu>
                <el-dropdown-item command="png">📸 导出高清图 (PNG)</el-dropdown-item>
                <el-dropdown-item command="csv">📊 导出数据表 (CSV)</el-dropdown-item>
              </el-dropdown-menu>
            </template>
          </el-dropdown>
        </div>
      </div>
    </template>

    <div class="trend-body">
      <!-- Period Statistics Ribbon -->
      <div class="stats-ribbon">
        <div class="stat-pill">
          <span class="pill-label">{{ t('market.chart.stats.high') }}</span>
          <span class="pill-value">{{ formatRate(stats.max) }}</span>
        </div>
        <div class="stat-pill">
          <span class="pill-label">{{ t('market.chart.stats.low') }}</span>
          <span class="pill-value">{{ formatRate(stats.min) }}</span>
        </div>
        <div class="stat-pill">
          <span class="pill-label">{{ t('market.chart.stats.avg') }}</span>
          <span class="pill-value">{{ formatRate(stats.avg) }}</span>
        </div>
        <div class="stat-pill">
          <span class="pill-label">{{ t('market.chart.stats.change') }}</span>
          <span
            class="pill-value"
            :class="stats.changeRate >= 0 ? 'text-success' : 'text-danger'"
          >
            {{ stats.changeRate >= 0 ? '+' : '' }}{{ stats.changeRate.toFixed(2) }}%
          </span>
        </div>
      </div>

      <!-- Chart Container -->
      <div ref="chartRef" class="chart-canvas" />
    </div>
  </el-card>
</template>

<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import { Icon } from '@iconify/vue'
import echarts, { type ECharts, type EChartsOption } from '@/utils/echarts'
import { useMarketStore } from '@/stores/market'
import { calculatePeriodStats, formatRate, calcCrossRate } from '@/utils/fx'
import { exportChartPng, exportChartCsv } from '@/utils/chart-export'

const { t } = useI18n()
const marketStore = useMarketStore()

const currencyOptions = ['USD', 'EUR', 'HKD', 'JPY', 'GBP', 'AUD', 'CAD', 'SGD']

const chartRef = ref<HTMLDivElement | null>(null)
let chartInstance: ECharts | null = null

// Normalized historical points adjusted by baseCurrency cross rate
const normalizedPoints = computed(() => {
  const baseCross = calcCrossRate('CNY', marketStore.baseCurrency, marketStore.rates)
  return marketStore.historyPoints.map((pt) => ({
    ...pt,
    rate: pt.rate * baseCross,
  }))
})

const stats = computed(() => calculatePeriodStats(normalizedPoints.value))

function updateChart() {
  if (!chartInstance && chartRef.value) {
    chartInstance = echarts.init(chartRef.value)
  }
  if (!chartInstance) return

  const dates = normalizedPoints.value.map((p) => p.rate_date)
  const values = normalizedPoints.value.map((p) => Number(p.rate.toFixed(4)))

  if (dates.length === 0) {
    chartInstance.clear()
    return
  }

  const option: EChartsOption = {
    tooltip: {
      trigger: 'axis',
      formatter: (params: any) => {
        const item = params[0]
        return `${item.name}<br/><b>1 ${marketStore.selectedCurrency} = ${item.value} ${marketStore.baseCurrency}</b>`
      },
    },
    grid: {
      top: '12%',
      left: '2%',
      right: '4%',
      bottom: '5%',
      containLabel: true,
    },
    xAxis: {
      type: 'category',
      data: dates,
      boundaryGap: false,
      axisLine: { lineStyle: { color: 'rgba(255, 255, 255, 0.08)' } },
      axisLabel: { color: 'var(--el-text-color-secondary)', fontSize: 11 },
    },
    yAxis: {
      type: 'value',
      scale: true,
      splitLine: { lineStyle: { color: 'rgba(255, 255, 255, 0.05)' } },
      axisLabel: { color: 'var(--el-text-color-secondary)', fontSize: 11 },
    },
    dataZoom: [
      {
        type: 'inside',
        start: 0,
        end: 100,
        zoomOnMouseWheel: true,
        moveOnMouseMove: true,
        moveOnMouseWheel: true,
      },
    ],
    series: [
      {
        name: `${marketStore.selectedCurrency}/${marketStore.baseCurrency}`,
        type: 'line',
        smooth: true,
        showSymbol: false,
        data: values,
        lineStyle: { width: 2.5, color: '#409EFF' },
        areaStyle: {
          color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
            { offset: 0, color: 'rgba(64, 158, 255, 0.35)' },
            { offset: 1, color: 'rgba(64, 158, 255, 0.02)' },
          ]),
        },
      },
    ],
  }

  chartInstance.setOption(option, true)
}

function handleExport(cmd: string) {
  if (!chartInstance) return
  const today = new Date().toISOString().slice(0, 10)
  const pair = `${marketStore.selectedCurrency}_${marketStore.baseCurrency}`
  if (cmd === 'png') {
    exportChartPng(chartInstance, `fx-trend-${pair}-${today}`)
  } else if (cmd === 'csv') {
    const headers = ['日期', '汇率', '基准币种']
    const rows = normalizedPoints.value.map((p) => [p.rate_date, p.rate, marketStore.baseCurrency])
    exportChartCsv(`fx-trend-${pair}-${today}.csv`, headers, rows)
  }
}

watch(
  () => [normalizedPoints.value, marketStore.baseCurrency],
  () => {
    updateChart()
  },
  { deep: true }
)

function handleResize() {
  chartInstance?.resize()
}

onMounted(() => {
  updateChart()
  window.addEventListener('resize', handleResize)
})

onUnmounted(() => {
  window.removeEventListener('resize', handleResize)
  chartInstance?.dispose()
  chartInstance = null
})
</script>

<style scoped>
.trend-card {
  border-radius: 10px;
  height: 100%;
}

.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.header-left {
  display: flex;
  align-items: center;
  gap: 12px;
}

.header-title {
  font-size: 15px;
  font-weight: 600;
  color: var(--el-text-color-primary);
}

.currency-pair-select {
  width: 140px;
}
.currency-pair-select :deep(.el-select__wrapper) {
  background: var(--mf-surface-muted);
  box-shadow: 0 0 0 1px var(--mf-border-subtle) inset;
  border-radius: var(--mf-radius-md);
}

.header-periods {
  display: flex;
  align-items: center;
  gap: 8px;
}

.chart-action-btn {
  background: var(--mf-surface-muted);
  border-color: var(--mf-border-subtle);
  color: var(--mf-text-muted);
  padding: 5px 8px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
}
.chart-action-btn:hover {
  color: var(--mf-primary);
  background: var(--mf-surface-hover);
}
.action-icon {
  font-size: 14px;
}

:deep(.el-radio-button__inner) {
  background: var(--mf-surface-muted) !important;
  border: 1px solid transparent !important;
  box-shadow: none !important;
  color: var(--mf-text-muted) !important;
  border-radius: var(--mf-radius-sm) !important;
  margin-right: 4px;
}
:deep(.el-radio-button:first-child .el-radio-button__inner) {
  border-left: 1px solid transparent !important;
}
:deep(.el-radio-button:hover .el-radio-button__inner) {
  color: var(--mf-primary) !important;
  background: var(--mf-surface-hover) !important;
}
:deep(.el-radio-button.is-active .el-radio-button__inner) {
  background: var(--mf-primary) !important;
  color: #ffffff !important;
  box-shadow: 0 0 8px rgba(59, 130, 246, 0.3) !important;
}

.trend-body {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.stats-ribbon {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 8px;
  background: var(--mf-surface-muted);
  border: 1px solid var(--mf-border-subtle);
  padding: 8px 12px;
  border-radius: var(--mf-radius-md);
}

.stat-pill {
  display: flex;
  flex-direction: column;
  align-items: center;
}

.pill-label {
  font-size: 11px;
  color: var(--el-text-color-secondary);
}

.pill-value {
  font-size: 13px;
  font-weight: 700;
  color: var(--el-text-color-primary);
}

.text-success {
  color: var(--el-color-success);
}

.text-danger {
  color: var(--el-color-danger);
}

.chart-canvas {
  width: 100%;
  height: 250px;
}
</style>
