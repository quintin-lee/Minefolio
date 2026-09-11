<template>
  <div ref="chartRef" style="height: 300px; width: 100%;"></div>
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted, watch } from 'vue'
import echarts, { type ECharts } from '@/utils/echarts'
import { resolveChartPalette, useChartThemeSync, makeAreaGradient, modernTooltipConfig, withAlpha } from '@/utils/echarts-theme'
import { formatCurrency } from '@/utils/format'

const props = defineProps<{ data: { date: string; net_worth: number }[] }>()
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
useChartThemeSync(updateChart)

function updateChart() {
  if (!chart || !props.data.length) return
  const P = resolveChartPalette()
  chart.setOption({
    animationDuration: 1000,
    animationEasing: 'cubicOut',
    tooltip: {
      ...modernTooltipConfig(P),
      formatter: (p: any) => {
        const item = p[0]
        const val = formatCurrency(item.value)
        return `<div style="font-size:12px;color:${P.textMuted};margin-bottom:4px;font-family:var(--mf-font-mono)">${item.name}</div>
                <div style="font-weight:700;font-family:var(--mf-font-mono);font-size:14px;color:${P.primary}">${item.seriesName}: ${val}</div>`
      }
    },
    grid: { left: 60, right: 20, top: 20, bottom: 30, containLabel: true },
    xAxis: {
      type: 'category',
      data: props.data.map((d) => d.date.slice(5)),
      axisLine: { lineStyle: { color: P.borderSubtle } },
      axisLabel: { color: P.textMuted, margin: 12, fontFamily: 'var(--mf-font-mono)' },
      axisTick: { show: false }
    },
    yAxis: {
      type: 'value',
      splitLine: { lineStyle: { type: 'dashed', color: P.borderSubtle } },
      axisLabel: {
        color: P.textMuted,
        fontFamily: 'var(--mf-font-mono)',
        formatter: (v: number) => (v >= 10000 ? `${(v / 10000).toFixed(1)}w` : v.toString())
      }
    },
    series: [{
      name: '净资产',
      type: 'line',
      data: props.data.map((d) => d.net_worth),
      smooth: 0.35,
      symbol: 'circle',
      symbolSize: 6,
      showSymbol: false,
      areaStyle: {
        color: makeAreaGradient(P.primary, 0.28, 0.0)
      },
      itemStyle: { color: P.primary, borderWidth: 2 },
      lineStyle: { width: 3, shadowColor: withAlpha(P.primary, 0.4), shadowBlur: 14 }
    }],
  })
}
</script>
