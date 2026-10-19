<template>
  <div class="price-history-container">
    <div class="chart-header">
      <div class="chart-title">
        <span>{{ assetName }} 历史净值走势</span>
        <el-tag size="small" type="info" class="ml-2">{{ currency }}</el-tag>
      </div>
      <div class="range-selector">
        <el-radio-group v-model="daysLimit" size="small" @change="loadHistory">
          <el-radio-button :value="30">近 30 天</el-radio-button>
          <el-radio-button :value="90">近 90 天</el-radio-button>
          <el-radio-button :value="365">近 1 年</el-radio-button>
        </el-radio-group>

        <el-dropdown trigger="click" @command="handleExport" class="chart-export-dropdown ml-2">
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
    <div v-loading="loading" ref="chartRef" class="chart-body" />
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, onBeforeUnmount, watch } from 'vue'
import { Icon } from '@iconify/vue'
import echarts, { type ECharts, type EChartsOption } from '@/utils/echarts'
import { marketApi } from '@/api/market'
import type { PriceHistoryItem } from '@/types'
import { resolveChartPalette, useChartThemeSync, withAlpha } from '@/utils/echarts-theme'
import { exportChartPng, exportChartCsv } from '@/utils/chart-export'

const props = defineProps<{
  assetId: number
  assetName: string
  currency?: string
}>()

const chartRef = ref<HTMLElement | null>(null)
let chart: ECharts | null = null
const loading = ref(false)
const daysLimit = ref(90)
const historyData = ref<PriceHistoryItem[]>([])

async function loadHistory() {
  if (!props.assetId) return
  loading.value = true
  try {
    const res = await marketApi.getHistory(props.assetId, daysLimit.value)
    historyData.value = res || []
    renderChart()
  } catch (err) {
    console.error('[PriceHistoryChart] load failed:', err)
  } finally {
    loading.value = false
  }
}

function renderChart() {
  if (!chartRef.value) return
  if (!chart) {
    chart = echarts.init(chartRef.value)
  }

  const dates = historyData.value.map(item => item.price_date)
  const prices = historyData.value.map(item => Number(item.price))

  const minPrice = prices.length > 0 ? Math.min(...prices) * 0.98 : 0
  const maxPrice = prices.length > 0 ? Math.max(...prices) * 1.02 : 100

  const P = resolveChartPalette()
  const option: EChartsOption = {
    tooltip: {
      trigger: 'axis',
      backgroundColor: P.surfaceCard,
      borderColor: P.border,
      textStyle: { color: P.textMain },
      formatter: (params: any) => {
        const p = params[0]
        return `${p.name}<br/><strong>净值/价格: ${Number(p.value).toFixed(4)} ${props.currency || 'CNY'}</strong>`
      }
    },
    grid: {
      left: '3%',
      right: '4%',
      bottom: '3%',
      top: '12%',
      containLabel: true
    },
    xAxis: {
      type: 'category',
      boundaryGap: false,
      data: dates,
      axisLine: { lineStyle: { color: P.border } }
    },
    yAxis: {
      type: 'value',
      min: parseFloat(minPrice.toFixed(4)),
      max: parseFloat(maxPrice.toFixed(4)),
      splitLine: { lineStyle: { color: P.borderSubtle } },
      axisLabel: {
        color: P.textMuted,
        formatter: (val: number) => val.toFixed(2)
      }
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
        name: '价格/净值',
        type: 'line',
        smooth: true,
        showSymbol: dates.length < 30,
        symbolSize: 6,
        data: prices,
        itemStyle: {
          color: P.primary
        },
        areaStyle: {
          color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
            { offset: 0, color: withAlpha(P.primary, 0.35) },
            { offset: 1, color: withAlpha(P.primary, 0.02) }
          ])
        }
      }
    ]
  }

  chart.setOption(option)
}

function handleExport(cmd: string) {
  if (!chart) return
  const today = new Date().toISOString().slice(0, 10)
  const safeName = (props.assetName || 'asset').replace(/[\\/:*?"<>|]/g, '_')
  if (cmd === 'png') {
    exportChartPng(chart, `${safeName}-price-history-${today}`)
  } else if (cmd === 'csv') {
    const headers = ['日期', '净值/价格', '币种']
    const cur = props.currency || 'CNY'
    const rows = historyData.value.map((item) => [item.price_date, item.price, cur])
    exportChartCsv(`${safeName}-price-history-${today}.csv`, headers, rows)
  }
}

function handleResize() {
  chart?.resize()
}

watch(() => props.assetId, () => {
  loadHistory()
})

onMounted(() => {
  loadHistory()
  window.addEventListener('resize', handleResize)
})

onBeforeUnmount(() => {
  window.removeEventListener('resize', handleResize)
  chart?.dispose()
  chart = null
})

useChartThemeSync(() => {
  if (chart) renderChart()
})
</script>

<style scoped>
.price-history-container {
  display: flex;
  flex-direction: column;
  height: 380px;
  width: 100%;
}
.chart-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 12px;
}
.chart-title {
  font-size: 15px;
  font-weight: 600;
  color: var(--el-text-color-primary);
  display: flex;
  align-items: center;
}
.ml-2 {
  margin-left: 8px;
}
.range-selector {
  display: flex;
  align-items: center;
}
.chart-action-btn {
  background: var(--mf-surface-muted);
  border-color: var(--mf-border);
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
.chart-body {
  flex: 1;
  width: 100%;
  min-height: 320px;
}
</style>
