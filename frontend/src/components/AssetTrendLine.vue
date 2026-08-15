<template>
  <div ref="chartRef" style="height: 100%; width: 100%;"></div>
</template>
<script setup lang="ts">
import { ref, onMounted, onUnmounted, watch } from 'vue'
import * as echarts from 'echarts'
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
function update() { 
  if (!chart || !props.data?.labels?.length) return
  chart.setOption({ 
    animationDuration: 1000,
    tooltip: {
      trigger: 'axis',
      backgroundColor: 'rgba(15, 23, 42, 0.95)',
      padding: [10, 15],
      textStyle: { color: '#e2e8f0' },
      borderColor: 'rgba(0, 212, 255, 0.2)',
      borderWidth: 1,
      shadowColor: 'rgba(0, 212, 255, 0.15)',
      shadowBlur: 16,
    },
    legend: {
      data: ['净资产', '总资产', '总负债'],
      top: 0,
      icon: 'circle',
      itemWidth: 10,
      itemHeight: 10,
      textStyle: { color: '#64748b' }
    },
    grid: { left: 50, right: 20, top: 40, bottom: 20, containLabel: true },
    xAxis: {
      type: 'category',
      data: props.data.labels,
      axisLine: { lineStyle: { color: 'rgba(0, 212, 255, 0.15)' } },
      axisLabel: { color: '#64748b', margin: 12 },
      axisTick: { show: false }
    },
    yAxis: {
      type: 'value',
      splitLine: { lineStyle: { type: 'dashed', color: 'rgba(0, 212, 255, 0.08)' } },
      axisLabel: {
        color: '#64748b',
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
        itemStyle: { color: '#00d4ff', borderWidth: 2 },
        lineStyle: { width: 2, shadowColor: 'rgba(0, 212, 255, 0.5)', shadowBlur: 12 },
        areaStyle: {
          color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
            { offset: 0, color: 'rgba(0, 212, 255, 0.2)' },
            { offset: 1, color: 'rgba(0, 212, 255, 0.02)' }
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
        itemStyle: { color: '#34d399', borderWidth: 2 },
        lineStyle: { width: 2 }
      },
      {
        name: '总负债',
        type: 'line',
        data: props.data.liabilities,
        smooth: 0.4,
        symbol: 'circle',
        symbolSize: 6,
        showSymbol: false,
        itemStyle: { color: '#f87171', borderWidth: 2 },
        lineStyle: { width: 2 }
      },
    ]
  })
}
</script>
