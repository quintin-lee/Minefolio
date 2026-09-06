<template>
  <div ref="chartRef" style="height: 300px; width: 100%;"></div>
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted, watch } from 'vue'
import * as echarts from 'echarts'
import { resolveChartPalette, useChartThemeSync } from '@/utils/echarts-theme'

const props = defineProps<{ data: { date: string; net_worth: number }[] }>()
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
  if (!chart || !props.data.length) return
  const P = resolveChartPalette()
  chart.setOption({
    animationDuration: 1000,
    animationEasing: 'cubicOut',
    tooltip: {
      trigger: 'axis',
      backgroundColor: P.surfaceCard,
      padding: [10, 15],
      textStyle: { color: P.textMain },
      borderColor: P.primaryBorder,
      borderWidth: 1,
      shadowColor: P.primaryLight,
      shadowBlur: 16,
      formatter: (p: any) => {
        const val = new Intl.NumberFormat('zh-CN', { style: 'currency', currency: 'CNY' }).format(p[0].value)
        return `<div style="font-size:12px;color:${P.textMuted};margin-bottom:4px">${p[0].name}</div>
                <div style="font-weight:bold;color:${P.primary}">${p[0].seriesName}: ${val}</div>`
      }
    },
    grid: { left: 60, right: 20, top: 20, bottom: 30, containLabel: true },
    xAxis: {
      type: 'category',
      data: props.data.map((d) => d.date.slice(5)),
      axisLine: { lineStyle: { color: P.primaryBorder } },
      axisLabel: { color: P.textMuted, margin: 12 },
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
    series: [{
      name: '净资产',
      type: 'line',
      data: props.data.map((d) => d.net_worth),
      smooth: 0.4,
      symbol: 'circle',
      symbolSize: 6,
      showSymbol: false,
      areaStyle: {
        color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
          { offset: 0, color: P.primaryLight },
          { offset: 1, color: P.primaryLight }
        ])
      },
      itemStyle: { color: P.primary, borderWidth: 2 },
      lineStyle: { width: 2, shadowColor: P.primaryLight, shadowBlur: 12 }
    }],
  })
}
</script>
