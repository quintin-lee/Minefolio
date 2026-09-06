<template>
  <div ref="chartRef" style="height: 280px; width: 100%;"></div>
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted, watch } from 'vue'
import * as echarts from 'echarts'
import { resolveChartPalette, shade, useChartThemeSync } from '@/utils/echarts-theme'

const props = defineProps<{ data: { total_income: number; total_expense: number } | null }>()
const chartRef = ref<HTMLElement>()
let chart: echarts.ECharts | null = null
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
useChartThemeSync(updateChart)

function updateChart() {
  if (!chart) return
  const d = props.data
  const P = resolveChartPalette()
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
        formatter: (v: number) => (v >= 10000 ? `${(v / 10000).toFixed(1)}w` : v.toString())
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
            { offset: 0, color: P.success },
            { offset: 1, color: shade(P.success, 0.72) }
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
            { offset: 0, color: P.danger },
            { offset: 1, color: shade(P.danger, 0.72) }
          ])
        }
      },
    ],
  })
}
</script>
