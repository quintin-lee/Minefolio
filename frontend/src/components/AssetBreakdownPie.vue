<template>
  <div ref="chartRef" style="height: 300px; width: 100%;"></div>
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted, watch } from 'vue'
import * as echarts from 'echarts'
import { resolveChartPalette, useChartThemeSync, withAlpha } from '@/utils/echarts-theme'
import { formatCurrency } from '@/utils/format'

const props = defineProps<{ data: { category_name: string; value: number; pct: number }[] }>()
const chartRef = ref<HTMLElement>()
let chart: echarts.ECharts | null = null
let resizeObserver: ResizeObserver | null = null

const colors = ['#3b82f6', '#10b981', '#f59e0b', '#f43f5e', '#6366f1', '#fb923c', '#0ea5e9', '#94a3b8']

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
  if (!chart || !props.data || props.data.length === 0) return
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
      textStyle: { color: P.textMuted, fontSize: 13 },
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
        borderColor: P.border,
        borderWidth: 2
      },
      label: {
        show: true,
        position: 'outside',
        formatter: '{b}: {d}%',
        color: P.textMuted,
        fontSize: 12,
        lineHeight: 18,
      },
      labelLine: {
        show: true,
        length: 12,
        length2: 20,
        lineStyle: { color: withAlpha(P.textMuted, 0.4) },
      },
      emphasis: {
        label: { show: true, fontSize: 14, fontWeight: 'bold', color: P.textMain },
        itemStyle: { shadowBlur: 16, shadowOffsetX: 0, shadowColor: P.primaryLight },
      },
      data: props.data.map((d) => ({ name: d.category_name, value: d.value, pct: d.pct })),
      color: colors,
    }],
  })
}
</script>
