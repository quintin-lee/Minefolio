<template>
  <div ref="chartRef" class="asset-price-line" />
</template>

<script setup lang="ts">
import { ref, onMounted, onBeforeUnmount, watch } from 'vue'
import * as echarts from 'echarts'
import type { PriceHistoryItem } from '@/types'

const props = defineProps<{ data: PriceHistoryItem[] }>()

const chartRef = ref<HTMLElement | null>(null)
let chart: echarts.ECharts | null = null

function render() {
  if (!chartRef.value) return
  if (!chart) {
    chart = echarts.init(chartRef.value)
  }
  const labels = props.data.map((p) => p.price_date)
  const values = props.data.map((p) => p.price)
  chart.setOption({
    grid: { left: 50, right: 20, top: 20, bottom: 30 },
    xAxis: {
      type: 'category',
      data: labels,
      axisLabel: { fontSize: 10, rotate: 30 },
    },
    yAxis: {
      type: 'value',
      axisLabel: { fontSize: 10 },
      splitLine: { lineStyle: { type: 'dashed', color: 'var(--mf-border)' } },
    },
    tooltip: { trigger: 'axis' },
    series: [
      {
        name: '净值',
        type: 'line',
        data: values,
        smooth: true,
        lineStyle: { width: 2, color: '#3b82f6' },
        areaStyle: {
          color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
            { offset: 0, color: 'rgba(59, 130, 246, 0.3)' },
            { offset: 1, color: 'rgba(59, 130, 246, 0.02)' },
          ]),
        },
        symbol: 'none',
      },
    ],
  })
}

onMounted(() => {
  render()
  window.addEventListener('resize', onResize)
})

onBeforeUnmount(() => {
  window.removeEventListener('resize', onResize)
  chart?.dispose()
})

watch(() => props.data, render, { deep: true })

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
