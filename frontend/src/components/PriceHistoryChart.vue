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
      </div>
    </div>
    <div v-loading="loading" ref="chartRef" class="chart-body" />
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, onBeforeUnmount, watch } from 'vue'
import * as echarts from 'echarts'
import { marketApi } from '@/api/market'
import type { PriceHistoryItem } from '@/types'

const props = defineProps<{
  assetId: number
  assetName: string
  currency?: string
}>()

const chartRef = ref<HTMLElement | null>(null)
let chart: echarts.ECharts | null = null
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

  const option: echarts.EChartsOption = {
    tooltip: {
      trigger: 'axis',
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
      axisLine: { lineStyle: { color: '#9ca3af' } }
    },
    yAxis: {
      type: 'value',
      min: parseFloat(minPrice.toFixed(4)),
      max: parseFloat(maxPrice.toFixed(4)),
      splitLine: { lineStyle: { color: '#f3f4f6' } },
      axisLabel: {
        formatter: (val: number) => val.toFixed(2)
      }
    },
    series: [
      {
        name: '价格/净值',
        type: 'line',
        smooth: true,
        showSymbol: dates.length < 30,
        symbolSize: 6,
        data: prices,
        itemStyle: {
          color: '#3b82f6'
        },
        areaStyle: {
          color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
            { offset: 0, color: 'rgba(59, 130, 246, 0.35)' },
            { offset: 1, color: 'rgba(59, 130, 246, 0.02)' }
          ])
        }
      }
    ]
  }

  chart.setOption(option)
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
.chart-body {
  flex: 1;
  width: 100%;
  min-height: 320px;
}
</style>
