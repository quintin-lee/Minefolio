<template>
  <div ref="chartRef" style="height: 100%; min-height: 280px; width: 100%;"></div>
</template>
<script setup lang="ts">
import { ref, onMounted, onUnmounted, watch } from 'vue'
import * as echarts from 'echarts'
import { resolveChartPalette, useChartThemeSync } from '@/utils/echarts-theme'
import { formatCurrency } from '@/utils/format'
const props = defineProps<{ data: { name: string; amount: number; pct: number }[] }>()
const chartRef = ref<HTMLElement>()
let chart: echarts.ECharts | null = null
let resizeObserver: ResizeObserver | null = null

const colors = ['#3b82f6', '#10b981', '#f59e0b', '#f43f5e', '#6366f1', '#fb923c', '#0ea5e9', '#94a3b8']
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
useChartThemeSync(update)
function update() { 
  if (!chart || !props.data.length) return
  const P = resolveChartPalette()
  chart.setOption({ 
    animationDuration: 1000,
    tooltip: {
      trigger: 'item',
      backgroundColor: P.surfaceCard,
      padding: [10, 15],
      textStyle: { color: P.textMain },
      borderColor: P.primaryBorder,
      borderWidth: 1,
      shadowColor: P.primaryLight,
      shadowBlur: 16,
      formatter: (p: any) => {
        const val = formatCurrency(p.data.value)
        return `<div style="font-weight:bold;color:${p.color};margin-bottom:4px">${p.data.name}</div>
                <div style="color:${P.textMuted}">金额: ${val}</div>
                <div style="color:${P.textMuted}">占比: ${p.data.pct.toFixed(2)}%</div>`
      }
    },
    legend: {
      orient: 'vertical',
      right: '5%',
      top: 'center',
      textStyle: { color: P.textMuted, fontSize: 12 },
      icon: 'circle',
      itemWidth: 8,
      itemHeight: 8,
      itemGap: 12
    },
    series: [{
      type: 'pie',
      radius: ['45%', '70%'],
      center: ['35%', '50%'],
      data: props.data.map(d => ({ name: d.name, value: d.amount, pct: d.pct })),
      avoidLabelOverlap: false,
      itemStyle: { borderRadius: 4, borderColor: P.border, borderWidth: 2 },
      label: { show: false, position: 'center' },
      emphasis: {
        label: {
          show: true,
          fontSize: 14,
          fontWeight: 'bold',
          color: P.textMain,
          formatter: '{b}\n{d}%'
        },
        itemStyle: {
          shadowBlur: 16,
          shadowOffsetX: 0,
          shadowColor: P.primaryLight
        }
      },
      labelLine: { show: false },
      color: colors
    }] 
  })
}
</script>
