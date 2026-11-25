<template>
  <div ref="chartRef" style="height: 100%; min-height: 280px; width: 100%;"></div>
</template>
<script setup lang="ts">
import { ref, onMounted, onUnmounted, watch } from 'vue'
import echarts, { type ECharts } from '@/utils/echarts'
import { resolveChartPalette, useChartThemeSync } from '@/utils/echarts-theme'
import { useDisplaySettingsStore } from '@/stores/display-settings'
import { formatCurrency } from '@/utils/format'
const props = defineProps<{ data: { name: string; amount: number; pct?: number; expense_type?: string; type?: string }[] }>()
const displaySettings = useDisplaySettingsStore()
const chartRef = ref<HTMLElement>()
let chart: ECharts | null = null
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
let lastIsNarrow = false
function handleResize() {
  if (chart) {
    chart.resize()
    const nowNarrow = (chartRef.value?.clientWidth || 0) > 0 && (chartRef.value?.clientWidth || 0) < 360
    if (nowNarrow !== lastIsNarrow) {
      update()
    }
  }
}
watch(() => props.data, update, { deep: true })
watch(() => displaySettings.showAmount, update)
useChartThemeSync(update)
function update() { 
  if (!chart) return
  const filtered = (props.data || []).filter((d) => {
    const t = d.expense_type ?? d.type
    return !t || t === 'expense'
  })
  const mergedMap = new Map<string, { name: string; amount: number; pct?: number }>()
  for (const item of filtered) {
    const prev = mergedMap.get(item.name)
    const amt = Number(item.amount || 0)
    if (prev) {
      prev.amount += amt
      prev.pct = undefined
    } else {
      mergedMap.set(item.name, { name: item.name, amount: amt, pct: item.pct })
    }
  }
  const merged = Array.from(mergedMap.values())
  if (!merged.length) {
    chart.clear()
    return
  }
  const total = merged.reduce((s, d) => s + d.amount, 0)
  const P = resolveChartPalette()
  const masked = !displaySettings.showAmount
  const width = chartRef.value?.clientWidth || 0
  const isNarrow = width > 0 && width < 360
  lastIsNarrow = isNarrow
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
        const val = masked ? '****' : formatCurrency(p.data.value)
        return `<div style="font-weight:bold;color:${p.color};margin-bottom:4px">${p.data.name}</div>
                <div style="color:${P.textMuted}">金额: ${val}</div>
                <div style="color:${P.textMuted}">占比: ${Number(p.data.pct ?? 0).toFixed(2)}%</div>`
      }
    },
    legend: isNarrow
      ? {
          orient: 'horizontal',
          bottom: 0,
          left: 'center',
          type: 'scroll',
          textStyle: { color: P.textMuted, fontSize: 11 },
          icon: 'circle',
          itemWidth: 8,
          itemHeight: 8,
          itemGap: 10,
        }
      : {
          orient: 'vertical',
          right: '5%',
          top: 'center',
          textStyle: { color: P.textMuted, fontSize: 12 },
          icon: 'circle',
          itemWidth: 8,
          itemHeight: 8,
          itemGap: 12,
        },
    series: [{
      type: 'pie',
      radius: isNarrow ? ['38%', '60%'] : ['45%', '70%'],
      center: isNarrow ? ['50%', '38%'] : ['35%', '50%'],
      data: merged.map(d => ({
        name: d.name,
        value: d.amount,
        pct: typeof d.pct === 'number' && Number.isFinite(d.pct) ? d.pct : (total > 0 ? (d.amount / total) * 100 : 0),
      })),
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
