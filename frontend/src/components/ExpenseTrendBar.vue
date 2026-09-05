<template>
  <div ref="chartRef" style="height: 100%; width: 100%;"></div>
</template>
<script setup lang="ts">
import { ref, onMounted, onUnmounted, watch } from 'vue'
import * as echarts from 'echarts'
const props = defineProps<{ data: { labels: string[]; income: number[]; expense: number[] } }>()
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
function update() { 
  if (!chart || !props.data?.labels?.length) return
  chart.setOption({ 
    animationDuration: 1000,
    tooltip: {
      trigger: 'axis',
      axisPointer: { type: 'shadow', shadowStyle: { color: 'var(--mf-primary-light)' } },
      backgroundColor: 'var(--mf-surface-card)',
      padding: [10, 15],
      textStyle: { color: 'var(--mf-text-main)' },
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
    grid: { left: 50, right: 20, top: 40, bottom: 20, containLabel: true },
    xAxis: {
      type: 'category',
      data: props.data.labels,
      axisLine: { lineStyle: { color: 'var(--mf-primary-border)' } },
      axisLabel: { color: '#64748b', margin: 12 },
      axisTick: { show: false }
    },
    yAxis: {
      type: 'value',
      splitLine: { lineStyle: { type: 'dashed', color: 'var(--mf-primary-light)' } },
      axisLabel: {
        color: '#64748b',
        formatter: (v: number) => v >= 10000 ? `${(v/10000).toFixed(1)}w` : String(v)
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
            { offset: 0, color: '#34d399' },
            { offset: 1, color: '#059669' }
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
            { offset: 0, color: '#f87171' },
            { offset: 1, color: '#dc2626' }
          ])
        }
      }
    ] 
  })
}
</script>
