<template>
  <div ref="chartRef" style="height: 300px; width: 100%;"></div>
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted, watch } from 'vue'
import * as echarts from 'echarts'

const props = defineProps<{ data: { category_name: string; value: number; pct: number }[] }>()
const chartRef = ref<HTMLElement>()
let chart: echarts.ECharts | null = null
let resizeObserver: ResizeObserver | null = null

const colors = ['#00d4ff', '#34d399', '#fbbf24', '#f87171', '#a78bfa', '#fb923c', '#38bdf8', '#94a3b8']

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
  if (!chart || !props.data.length) return
  chart.setOption({
    animationDuration: 1000,
    tooltip: {
      trigger: 'item',
      backgroundColor: 'rgba(15, 23, 42, 0.95)',
      padding: [10, 15],
      textStyle: { color: '#e2e8f0' },
      borderColor: 'rgba(0, 212, 255, 0.2)',
      borderWidth: 1,
      shadowColor: 'rgba(0, 212, 255, 0.15)',
      shadowBlur: 16,
      formatter: (p: any) => {
        const val = new Intl.NumberFormat('zh-CN', { style: 'currency', currency: 'CNY' }).format(p.data.value)
        return `<div style="font-weight:bold;color:${p.color};margin-bottom:4px">${p.data.name}</div>
                <div style="color:#64748b">金额: ${val}</div>
                <div style="color:#64748b">占比: ${p.data.pct.toFixed(2)}%</div>`
      }
    },
    legend: {
      orient: 'vertical',
      right: '5%',
      top: 'center',
      textStyle: { color: '#64748b', fontSize: 13 },
      icon: 'circle',
      itemWidth: 10,
      itemHeight: 10,
      itemGap: 16
    },
    series: [{
      type: 'pie',
      radius: ['50%', '75%'],
      center: ['42%', '50%'],
      avoidLabelOverlap: false,
      itemStyle: {
        borderRadius: 6,
        borderColor: 'rgba(6,11,24,0.8)',
        borderWidth: 2
      },
      label: {
        show: true,
        position: 'outside',
        formatter: '{b}: {d}%',
        color: '#94a3b8',
        fontSize: 12,
        lineHeight: 18,
      },
      labelLine: {
        show: true,
        length: 12,
        length2: 20,
        lineStyle: { color: 'rgba(148,163,184,0.4)' },
      },
      emphasis: {
        label: { show: true, fontSize: 14, fontWeight: 'bold', color: '#e2e8f0' },
        itemStyle: { shadowBlur: 16, shadowOffsetX: 0, shadowColor: 'rgba(0, 212, 255, 0.4)' },
      },
      data: props.data.map((d) => ({ name: d.category_name, value: d.value, pct: d.pct })),
      color: colors,
    }],
  })
}
</script>
