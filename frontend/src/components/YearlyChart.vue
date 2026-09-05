<template>
  <div ref="chartRef" style="height: 280px; width: 100%;"></div>
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted, watch } from 'vue'
import * as echarts from 'echarts'
import type { ExpenseYearlyReport } from '@/api/reports'

const props = defineProps<{ data: ExpenseYearlyReport | null }>()
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

function updateChart() {
  if (!chart) return
  const d = props.data
  const labels = d?.labels ?? []
  const income = d?.income ?? []
  const expense = d?.expense ?? []
  chart.setOption({
    animationDuration: 1000,
    tooltip: {
      trigger: 'axis',
      axisPointer: { type: 'shadow', shadowStyle: { color: 'var(--mf-primary-light)' } },
      backgroundColor: 'var(--mf-surface-card)',
      padding: [10, 15],
      textStyle: { color: '#e2e8f0' },
      borderColor: 'var(--mf-primary-border)',
      borderWidth: 1,
      shadowColor: 'var(--mf-primary-light)',
      shadowBlur: 16,
    },
    legend: {
      data: ['收入', '支出'],
      top: 0,
      icon: 'roundRect',
      itemWidth: 16,
      itemHeight: 8,
      textStyle: { color: 'var(--mf-text-muted)' }
    },
    grid: { left: 60, right: 20, top: 40, bottom: 20, containLabel: true },
    xAxis: {
      type: 'category',
      data: labels,
      axisLine: { lineStyle: { color: 'var(--mf-primary-border)' } },
      axisLabel: { color: 'var(--mf-text-muted)' },
      axisTick: { show: false }
    },
    yAxis: {
      type: 'value',
      splitLine: { lineStyle: { type: 'dashed', color: 'var(--mf-primary-light)' } },
      axisLabel: {
        color: 'var(--mf-text-muted)',
        formatter: (v: number) => (v >= 10000 ? `${(v / 10000).toFixed(1)}w` : v.toString())
      }
    },
    series: [
      {
        name: '收入',
        type: 'bar',
        barMaxWidth: 22,
        data: income,
        itemStyle: {
          borderRadius: [4, 4, 0, 0],
          color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
            { offset: 0, color: '#34d399' },
            { offset: 1, color: '#059669' }
          ])
        }
      },
      {
        name: '支出',
        type: 'bar',
        barMaxWidth: 22,
        data: expense,
        itemStyle: {
          borderRadius: [4, 4, 0, 0],
          color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
            { offset: 0, color: '#f87171' },
            { offset: 1, color: '#dc2626' }
          ])
        }
      },
    ],
  })
}
</script>
