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
      axisPointer: { type: 'shadow', shadowStyle: { color: 'rgba(0,212,255,0.05)' } },
      backgroundColor: 'rgba(15, 23, 42, 0.95)',
      padding: [10, 15],
      textStyle: { color: '#e2e8f0' },
      borderColor: 'rgba(0, 212, 255, 0.2)',
      borderWidth: 1,
      shadowColor: 'rgba(0, 212, 255, 0.15)',
      shadowBlur: 16,
    },
    legend: {
      data: ['收入', '支出'],
      top: 0,
      icon: 'roundRect',
      itemWidth: 16,
      itemHeight: 8,
      textStyle: { color: '#64748b' }
    },
    grid: { left: 60, right: 20, top: 40, bottom: 20, containLabel: true },
    xAxis: {
      type: 'category',
      data: labels,
      axisLine: { lineStyle: { color: 'rgba(0, 212, 255, 0.15)' } },
      axisLabel: { color: '#64748b' },
      axisTick: { show: false }
    },
    yAxis: {
      type: 'value',
      splitLine: { lineStyle: { type: 'dashed', color: 'rgba(0, 212, 255, 0.08)' } },
      axisLabel: {
        color: '#64748b',
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
