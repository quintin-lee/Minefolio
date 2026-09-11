<template>
  <div class="net-worth-chart-container">
    <div class="chart-header-actions">
      <el-radio-group v-model="timeRange" size="small" class="pill-radio-group" @change="updateChart">
        <el-radio-button label="30D">30D</el-radio-button>
        <el-radio-button label="90D">90D</el-radio-button>
        <el-radio-button label="1Y">1Y</el-radio-button>
        <el-radio-button label="ALL">全部</el-radio-button>
      </el-radio-group>
    </div>
    <div ref="chartRef" style="height: 285px; width: 100%;"></div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted, watch, computed } from 'vue'
import echarts, { type ECharts } from '@/utils/echarts'
import { resolveChartPalette, useChartThemeSync, makeAreaGradient, modernTooltipConfig, withAlpha } from '@/utils/echarts-theme'
import { formatCurrency } from '@/utils/format'

const props = defineProps<{ data: { date: string; net_worth: number }[] }>()
const chartRef = ref<HTMLElement>()
const timeRange = ref<'30D' | '90D' | '1Y' | 'ALL'>('ALL')
let chart: ECharts | null = null
let resizeObserver: ResizeObserver | null = null

const activeData = computed(() => {
  const d = props.data || []
  if (d.length === 0 || timeRange.value === 'ALL') return d
  const count = timeRange.value === '30D' ? 30 : timeRange.value === '90D' ? 90 : 365
  return d.slice(-count)
})

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
  if (!chart || !activeData.value.length) return
  const P = resolveChartPalette()
  const data = activeData.value
  const firstVal = data[0]?.net_worth || 0

  chart.setOption({
    animationDuration: 800,
    animationEasing: 'cubicOut',
    tooltip: {
      ...modernTooltipConfig(P),
      trigger: 'axis',
      axisPointer: {
        type: 'line',
        lineStyle: {
          color: withAlpha(P.primary, 0.45),
          width: 1.5,
          type: 'dashed'
        }
      },
      formatter: (p: any) => {
        const item = p[0]
        const val = formatCurrency(item.value)
        const diff = item.value - firstVal
        const diffPct = firstVal !== 0 ? ((diff / Math.abs(firstVal)) * 100).toFixed(2) : '0.00'
        const sign = diff >= 0 ? '+' : ''
        const diffColor = diff >= 0 ? P.success : P.danger
        return `<div style="font-size:12px;color:${P.textMuted};margin-bottom:4px;font-family:var(--mf-font-mono)">${item.name}</div>
                <div style="display:flex;align-items:baseline;justify-content:space-between;gap:12px">
                  <span style="font-weight:700;font-family:var(--mf-font-mono);font-size:15px;color:${P.textMain}">${val}</span>
                  <span style="font-size:12px;font-weight:600;font-family:var(--mf-font-mono);color:${diffColor}">${sign}${diffPct}%</span>
                </div>`
      }
    },
    grid: { left: 60, right: 20, top: 15, bottom: 30, containLabel: true },
    xAxis: {
      type: 'category',
      data: data.map((d) => d.date.slice(5)),
      axisLine: { lineStyle: { color: P.borderSubtle } },
      axisLabel: { color: P.textMuted, margin: 12, fontFamily: 'var(--mf-font-mono)' },
      axisTick: { show: false }
    },
    yAxis: {
      type: 'value',
      splitLine: { lineStyle: { type: 'dashed', color: P.isLight ? 'rgba(0, 0, 0, 0.06)' : 'rgba(255, 255, 255, 0.04)' } },
      axisLabel: {
        color: P.textMuted,
        fontFamily: 'var(--mf-font-mono)',
        formatter: (v: number) => (v >= 10000 ? `${(v / 10000).toFixed(1)}w` : v.toString())
      }
    },
    series: [{
      name: '净资产',
      type: 'line',
      data: data.map((d) => d.net_worth),
      smooth: 0.35,
      symbol: 'circle',
      symbolSize: 6,
      showSymbol: false,
      areaStyle: {
        color: makeAreaGradient(P.primary, 0.25, 0.0)
      },
      itemStyle: { color: P.accent, borderWidth: 2 },
      lineStyle: {
        width: 3,
        color: new echarts.graphic.LinearGradient(0, 0, 1, 0, [
          { offset: 0, color: P.primary },
          { offset: 1, color: P.accent }
        ]),
        shadowColor: withAlpha(P.accent, 0.45),
        shadowBlur: 14
      }
    }],
  })
}
</script>

<style scoped>
.net-worth-chart-container {
  position: relative;
  width: 100%;
}
.chart-header-actions {
  display: flex;
  justify-content: flex-end;
  margin-bottom: 6px;
}
:deep(.pill-radio-group .el-radio-button__inner) {
  background: var(--mf-surface-muted);
  border-color: var(--mf-border);
  color: var(--mf-text-muted);
  font-size: 11px;
  padding: 5px 10px;
  transition: var(--mf-transition);
}
:deep(.pill-radio-group .el-radio-button:hover .el-radio-button__inner) {
  color: var(--mf-primary);
  background: var(--mf-surface-hover);
}
:deep(.pill-radio-group .el-radio-button.is-active .el-radio-button__inner),
:deep(.pill-radio-group .el-radio-button__original-radio:checked + .el-radio-button__inner) {
  background: var(--mf-primary) !important;
  border-color: var(--mf-primary) !important;
  color: #ffffff !important;
  box-shadow: 0 0 10px rgba(59, 130, 246, 0.35);
}
</style>
