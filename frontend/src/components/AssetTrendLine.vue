<template>
  <div ref="chartRef" style="height: 100%; width: 100%;"></div>
</template>
<script setup lang="ts">
import { ref, onMounted, onUnmounted, watch } from 'vue'
import * as echarts from 'echarts'
import { resolveChartPalette, useChartThemeSync, withAlpha } from '@/utils/echarts-theme'
const props = defineProps<{ data: { labels: string[]; net_worth: number[]; assets: number[]; liabilities: number[] } }>()
const chartRef = ref<HTMLElement>()
let chart: echarts.ECharts | null = null
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
useChartThemeSync(update)
function update() {
  if (!chart || !props.data?.labels?.length) return
  const P = resolveChartPalette()
  chart.setOption({
    animationDuration: 1000,
    tooltip: {
      trigger: 'axis',
      backgroundColor: P.surfaceCard,
      padding: [10, 15],
      textStyle: { color: P.textMain },
      borderColor: P.border,
      borderWidth: 1,
      shadowColor: P.primaryLight,
      shadowBlur: 16,
    },
    grid: { left: 50, right: 20, top: 40, bottom: 20, containLabel: true },
    xAxis: {
      type: 'category',
      data: props.data.labels,
      axisLine: { lineStyle: { color: P.primaryBorder } },
      axisLabel: { color: P.textMuted, margin: 12 }
    },
    yAxis: {
      type: 'value',
      splitLine: { lineStyle: { type: 'dashed', color: P.primaryLight } },
      axisLabel: {
        color: P.textMuted,
        formatter: (v: number) => v >= 10000 ? `${(v/10000).toFixed(1)}w` : String(v)
      }
    },
    series: [
      {
        name: '净资产',
        type: 'line',
        data: props.data.net_worth,
        smooth: 0.4,
        symbol: 'circle',
        symbolSize: 6,
        showSymbol: false,
        itemStyle: { color: P.primary, borderWidth: 2 },
        lineStyle: { width: 2, shadowColor: P.primaryLight, shadowBlur: 12 },
        areaStyle: {
          color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
            { offset: 0, color: withAlpha(P.primary, 0.25) },
            { offset: 1, color: P.primaryLight }
          ])
        }
      },
      {
        name: '总资产',
        type: 'line',
        data: props.data.assets,
        smooth: 0.4,
        symbol: 'circle',
        symbolSize: 6,
        showSymbol: false,
        itemStyle: { color: P.success, borderWidth: 2 },
      },
      {
        name: '总负债',
        type: 'line',
        data: props.data.liabilities,
        smooth: 0.4,
        symbol: 'circle',
        symbolSize: 6,
        showSymbol: false,
        itemStyle: { color: P.danger, borderWidth: 2 },
      },
    ]
  })
}
</script>
