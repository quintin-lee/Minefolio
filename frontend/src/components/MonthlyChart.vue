<template>
  <div ref="chartRef" class="chart-canvas" :style="{ minHeight: props.minHeight }"></div>
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted, watch } from 'vue'
import echarts, { type ECharts } from '@/utils/echarts'
import { resolveChartPalette, shade, useChartThemeSync } from '@/utils/echarts-theme'
import { useDisplaySettingsStore } from '@/stores/display-settings'

const props = withDefaults(defineProps<{
  data: { total_income: number; total_expense: number } | null
  minHeight?: string
}>(), {
  minHeight: '280px',
})
const displaySettings = useDisplaySettingsStore()
const chartRef = ref<HTMLElement>()
let chart: ECharts | null = null
let resizeObserver: ResizeObserver | null = null

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
  if (chartRef.value) {
    resizeObserver = new ResizeObserver(() => { ensureChart(); handleResize() })
    resizeObserver.observe(chartRef.value)
  }
})

onUnmounted(() => {
  window.removeEventListener('resize', handleResize)
  if (resizeObserver) resizeObserver.disconnect()
  if (chart) {
    chart.dispose()
    chart = null
  }
})

function handleResize() {
  if (chart) chart.resize()
}

watch(() => props.data, updateChart, { deep: true })
watch(() => displaySettings.showAmount, updateChart)
useChartThemeSync(updateChart)

function updateChart() {
  if (!chart) return
  const d = props.data
  const P = resolveChartPalette()
  const masked = !displaySettings.showAmount
  chart.setOption({
    animationDuration: 1000,
    tooltip: {
      trigger: 'axis',
      axisPointer: { type: 'shadow', shadowStyle: { color: P.primaryLight } },
      backgroundColor: P.surfaceCard,
      padding: [10, 15],
      textStyle: { color: P.textMain },
      borderColor: P.primaryBorder,
      borderWidth: 1,
      shadowColor: P.primaryLight,
      shadowBlur: 16,
      valueFormatter: (v: any) => (masked ? '****' : String(v)),
    },
    legend: {
      data: ['收入', '支出'],
      top: 0,
      icon: 'roundRect',
      itemWidth: 16,
      itemHeight: 8,
      textStyle: { color: P.textMuted }
    },
    grid: { left: 60, right: 20, top: 40, bottom: 20, containLabel: true },
    xAxis: {
      type: 'category',
      data: ['本月'],
      axisLine: { lineStyle: { color: P.primaryBorder } },
      axisLabel: { color: P.textMuted },
      axisTick: { show: false }
    },
    yAxis: {
      type: 'value',
      splitLine: { lineStyle: { type: 'dashed', color: P.primaryLight } },
      axisLabel: {
        color: P.textMuted,
        formatter: (v: number) => (masked ? '****' : (v >= 10000 ? `${(v / 10000).toFixed(1)}w` : v.toString()))
      }
    },
    series: [
      {
        name: '收入',
        type: 'bar',
        barWidth: 40,
        data: [d?.total_income ?? 0],
        itemStyle: {
          borderRadius: [6, 6, 0, 0],
          color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
            { offset: 0, color: P.income },
            { offset: 1, color: shade(P.income, 0.72) }
          ])
        }
      },
      {
        name: '支出',
        type: 'bar',
        barWidth: 40,
        data: [d?.total_expense ?? 0],
        itemStyle: {
          borderRadius: [6, 6, 0, 0],
          color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
            { offset: 0, color: P.expense },
            { offset: 1, color: shade(P.expense, 0.72) }
          ])
        }
      },
    ],
  })
}
</script>

<style scoped>
.chart-canvas {
  width: 100%;
  height: 100%;
}
</style>
