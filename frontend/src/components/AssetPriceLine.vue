<template>
  <div ref="chartRef" class="asset-price-line" />
</template>

<script setup lang="ts">
import { ref, onMounted, onBeforeUnmount, watch } from 'vue'
import echarts, { type ECharts } from '@/utils/echarts'
import { resolveChartPalette, useChartThemeSync, withAlpha } from '@/utils/echarts-theme'
import { useDisplaySettingsStore } from '@/stores/display-settings'
import type { PriceHistoryItem } from '@/types'

const props = defineProps<{ data: PriceHistoryItem[] }>()

const chartRef = ref<HTMLElement | null>(null)
const displaySettings = useDisplaySettingsStore()
let chart: ECharts | null = null
let resizeObserver: ResizeObserver | null = null

function ensureChart() {
  if (chart || !chartRef.value) return
  const el = chartRef.value
  if (!el.clientWidth || !el.clientHeight) return
  chart = echarts.init(el)
  render()
}

function render() {
  if (!chartRef.value) return
  if (!chart) {
    if (!chartRef.value.clientWidth || !chartRef.value.clientHeight) return
    chart = echarts.init(chartRef.value)
  }
  const P = resolveChartPalette()
  const masked = !displaySettings.showAmount
  const labels = props.data.map((p) => p.price_date)
  const values = props.data.map((p) => p.price)
  chart.setOption({
    grid: { left: 50, right: 20, top: 20, bottom: 30 },
    xAxis: {
      type: 'category',
      data: labels,
      axisLabel: { fontSize: 10, rotate: 30, color: P.textMuted },
      axisLine: { lineStyle: { color: P.borderSubtle } },
    },
    yAxis: {
      type: 'value',
      axisLabel: {
        fontSize: 10,
        color: P.textMuted,
        formatter: (v: number) => (masked ? '****' : String(v)),
      },
      splitLine: { lineStyle: { type: 'dashed', color: P.borderSubtle } },
    },
    tooltip: {
      trigger: 'axis',
      backgroundColor: P.surfaceCard,
      borderColor: P.border,
      textStyle: { color: P.textMain },
      valueFormatter: (v: any) => (masked ? '****' : String(v)),
    },
    series: [
      {
        name: '净值',
        type: 'line',
        data: values,
        smooth: true,
        lineStyle: { width: 2, color: P.primary },
        areaStyle: {
          color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
            { offset: 0, color: withAlpha(P.primary, 0.3) },
            { offset: 1, color: withAlpha(P.primary, 0.02) },
          ]),
        },
        symbol: 'none',
      },
    ],
  })
}

onMounted(() => {
  ensureChart()
  window.addEventListener('resize', onResize)
  if (chartRef.value) {
    resizeObserver = new ResizeObserver(() => {
      ensureChart()
      onResize()
    })
    resizeObserver.observe(chartRef.value)
  }
})

onBeforeUnmount(() => {
  window.removeEventListener('resize', onResize)
  resizeObserver?.disconnect()
  resizeObserver = null
  if (chart) {
    chart.dispose()
    chart = null
  }
})

watch(() => props.data, render, { deep: true })
watch(() => displaySettings.showAmount, render)
useChartThemeSync(render)

function onResize() {
  chart?.resize()
}
</script>

<style scoped>
.asset-price-line {
  width: 100%;
  height: 100%;
  min-height: 180px;
}
</style>
