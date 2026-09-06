<template>
  <div ref="chartRef" class="holdings-bar-chart" />
</template>

<script setup lang="ts">
import { onMounted, onUnmounted, ref, watch } from 'vue'
import * as echarts from 'echarts'
import { resolveChartPalette, useChartThemeSync } from '@/utils/echarts-theme'

export interface HoldingsBarDatum {
  name: string
  cost_basis: number
  current_value: number
}

const props = defineProps<{ data: HoldingsBarDatum[] }>()

const chartRef = ref<HTMLDivElement | null>(null)
let chart: echarts.ECharts | null = null
let resizeObserver: ResizeObserver | null = null

function updateChart() {
  if (!chart) return
  const P = resolveChartPalette()
  chart.setOption({
    tooltip: {
      trigger: 'axis',
      axisPointer: { type: 'shadow' },
      backgroundColor: P.surfaceCard,
      borderColor: P.border,
      textStyle: { color: P.textMain },
    },
    legend: { bottom: 0, textStyle: { color: P.textMuted } },
    grid: { left: 16, right: 16, top: 32, bottom: 48, containLabel: true },
    xAxis: {
      type: 'category',
      data: props.data.map((d) => d.name),
      axisLabel: { color: P.textMuted, interval: 0, rotate: props.data.length > 4 ? 30 : 0 },
    },
    yAxis: {
      type: 'value',
      axisLabel: { color: P.textMuted },
      splitLine: { lineStyle: { color: P.borderSubtle } },
    },
    series: [
      {
        name: '成本',
        type: 'bar',
        data: props.data.map((d) => d.cost_basis),
        itemStyle: { color: P.textMuted },
        barMaxWidth: 24,
      },
      {
        name: '市值',
        type: 'bar',
        data: props.data.map((d) => d.current_value),
        itemStyle: { color: P.primary },
        barMaxWidth: 24,
      },
    ],
  })
}

function handleResize() {
  chart?.resize()
}

function ensureChart() {
  if (chart || !chartRef.value) return
  const el = chartRef.value
  if (!el.clientWidth || !el.clientHeight) return
  chart = echarts.init(el)
  updateChart()
}
onMounted(() => {
  ensureChart()
  window.addEventListener('resize', handleResize)
  resizeObserver = new ResizeObserver(() => { ensureChart(); handleResize() })
  resizeObserver.observe(chartRef.value!)
})

onUnmounted(() => {
  window.removeEventListener('resize', handleResize)
  resizeObserver?.disconnect()
  chart?.dispose()
})

watch(() => props.data, updateChart, { deep: true })
useChartThemeSync(updateChart)
</script>

<style scoped>
.holdings-bar-chart {
  width: 100%;
  height: 280px;
}
</style>