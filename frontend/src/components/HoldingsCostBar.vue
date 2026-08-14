<template>
  <div ref="chartRef" class="holdings-bar-chart" />
</template>

<script setup lang="ts">
import { onMounted, onUnmounted, ref, watch } from 'vue'
import * as echarts from 'echarts'

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
  chart.setOption({
    tooltip: {
      trigger: 'axis',
      axisPointer: { type: 'shadow' },
      backgroundColor: 'rgba(15,23,42,0.95)',
      borderColor: '#334155',
      textStyle: { color: '#e2e8f0' },
    },
    legend: { bottom: 0, textStyle: { color: '#94a3b8' } },
    grid: { left: 16, right: 16, top: 32, bottom: 48, containLabel: true },
    xAxis: {
      type: 'category',
      data: props.data.map((d) => d.name),
      axisLabel: { color: '#94a3b8', interval: 0, rotate: props.data.length > 4 ? 30 : 0 },
    },
    yAxis: {
      type: 'value',
      axisLabel: { color: '#94a3b8' },
      splitLine: { lineStyle: { color: '#1e293b' } },
    },
    series: [
      {
        name: '成本',
        type: 'bar',
        data: props.data.map((d) => d.cost_basis),
        itemStyle: { color: '#475569' },
        barMaxWidth: 24,
      },
      {
        name: '市值',
        type: 'bar',
        data: props.data.map((d) => d.current_value),
        itemStyle: { color: '#00d4ff' },
        barMaxWidth: 24,
      },
    ],
  })
}

function handleResize() {
  chart?.resize()
}

onMounted(() => {
  chart = echarts.init(chartRef.value!)
  updateChart()
  window.addEventListener('resize', handleResize)
  resizeObserver = new ResizeObserver(handleResize)
  resizeObserver.observe(chartRef.value!)
})

onUnmounted(() => {
  window.removeEventListener('resize', handleResize)
  resizeObserver?.disconnect()
  chart?.dispose()
})

watch(() => props.data, updateChart, { deep: true })
</script>

<style scoped>
.holdings-bar-chart {
  width: 100%;
  height: 280px;
}
</style>