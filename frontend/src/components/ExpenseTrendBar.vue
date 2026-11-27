<template>
  <div ref="chartRef" style="height: 100%; width: 100%;"></div>
</template>
<script setup lang="ts">
import { ref, onMounted, onUnmounted, watch } from 'vue'
import echarts, { type ECharts } from '@/utils/echarts'
import { resolveChartPalette, shade, useChartThemeSync } from '@/utils/echarts-theme'
import { useDisplaySettingsStore } from '@/stores/display-settings'
const props = defineProps<{ data: { labels: string[]; income: number[]; expense: number[] } }>()
const displaySettings = useDisplaySettingsStore()
const chartRef = ref<HTMLElement>()
let chart: ECharts | null = null
let resizeObserver: ResizeObserver | null = null

function ensureChart() {
  if (chart || !chartRef.value) return
  const el = chartRef.value
  if (!el.clientWidth || !el.clientHeight) return
  chart = echarts.init(el)
  update()
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
watch(() => props.data, update, { deep: true })
watch(() => displaySettings.showAmount, update)
useChartThemeSync(update)
function update() { 
  if (!chart || !props.data?.labels?.length) return
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
    grid: { left: 50, right: 20, top: 40, bottom: 20, containLabel: true },
    xAxis: {
      type: 'category',
      data: props.data.labels,
      axisLine: { lineStyle: { color: P.primaryBorder } },
      axisLabel: { color: P.textMuted, margin: 12 },
      axisTick: { show: false }
    },
    yAxis: {
      type: 'value',
      splitLine: { lineStyle: { type: 'dashed', color: P.primaryLight } },
      axisLabel: {
        color: P.textMuted,
        formatter: (v: number) => (masked ? '****' : (v >= 10000 ? `${(v/10000).toFixed(1)}w` : String(v)))
      }
    },
    series: [
      {
        name: '收入',
        type: 'bar',
        barMaxWidth: 30,
        data: props.data.income,
        itemStyle: {
          borderRadius: [4, 4, 0, 0],
          color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
            { offset: 0, color: P.income },
            { offset: 1, color: shade(P.income, 0.72) }
          ])
        }
      },
      {
        name: '支出',
        type: 'bar',
        barMaxWidth: 30,
        data: props.data.expense,
        itemStyle: {
          borderRadius: [4, 4, 0, 0],
          color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
            { offset: 0, color: P.expense },
            { offset: 1, color: shade(P.expense, 0.72) }
          ])
        }
      }
    ] 
  })
}
</script>
