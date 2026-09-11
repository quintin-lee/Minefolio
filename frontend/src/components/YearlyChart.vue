<template>
  <div ref="chartRef" style="height: 285px; width: 100%;"></div>
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted, watch } from 'vue'
import echarts, { type ECharts } from '@/utils/echarts'
import type { ExpenseYearlyReport } from '@/api/reports'
import { resolveChartPalette, useChartThemeSync, modernTooltipConfig } from '@/utils/echarts-theme'

const props = defineProps<{ data: ExpenseYearlyReport | null }>()
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
  if (!chart) return
  const d = props.data
  const P = resolveChartPalette()
  const labels = d?.labels ?? []
  const income = d?.income ?? []
  const expense = d?.expense ?? []
  chart.setOption({
    animationDuration: 800,
    tooltip: {
      ...modernTooltipConfig(P),
      trigger: 'axis',
      axisPointer: { type: 'shadow', shadowStyle: { color: P.primaryLight } },
    },
    legend: {
      data: ['收入', '支出'],
      top: 0,
      icon: 'roundRect',
      itemWidth: 14,
      itemHeight: 6,
      textStyle: { color: P.textMuted, fontSize: 11, fontFamily: 'var(--mf-font-mono)' }
    },
    grid: { left: 55, right: 15, top: 35, bottom: 20, containLabel: true },
    xAxis: {
      type: 'category',
      data: labels,
      axisLine: { lineStyle: { color: P.borderSubtle } },
      axisLabel: { color: P.textMuted, fontSize: 11, fontFamily: 'var(--mf-font-mono)' },
      axisTick: { show: false }
    },
    yAxis: {
      type: 'value',
      splitLine: { lineStyle: { type: 'dashed', color: P.isLight ? 'rgba(0, 0, 0, 0.06)' : 'rgba(255, 255, 255, 0.04)' } },
      axisLabel: {
        color: P.textMuted,
        fontFamily: 'var(--mf-font-mono)',
        fontSize: 11,
        formatter: (v: number) => (v >= 10000 ? `${(v / 10000).toFixed(1)}w` : v.toString())
      }
    },
    series: [
      {
        name: '收入',
        type: 'bar',
        barMaxWidth: 18,
        data: income,
        itemStyle: {
          borderRadius: [4, 4, 0, 0],
          color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
            { offset: 0, color: '#10b981' },
            { offset: 1, color: '#059669' }
          ])
        },
        emphasis: {
          itemStyle: {
            shadowBlur: 10,
            shadowColor: 'rgba(16, 185, 129, 0.4)'
          }
        }
      },
      {
        name: '支出',
        type: 'bar',
        barMaxWidth: 18,
        data: expense,
        itemStyle: {
          borderRadius: [4, 4, 0, 0],
          color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
            { offset: 0, color: '#f43f5e' },
            { offset: 1, color: '#e11d48' }
          ])
        },
        emphasis: {
          itemStyle: {
            shadowBlur: 10,
            shadowColor: 'rgba(244, 63, 94, 0.4)'
          }
        }
      },
    ],
  })
}
</script>
