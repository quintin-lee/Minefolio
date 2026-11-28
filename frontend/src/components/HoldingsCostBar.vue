<template>
  <div class="holdings-bar-wrap">
    <div v-if="hasData" ref="chartRef" class="holdings-bar-chart" />
    <el-empty v-else description="暂无成本与市值对比数据" :image-size="64" />
  </div>
</template>

<script setup lang="ts">
import { computed, nextTick, onMounted, onUnmounted, ref, watch } from 'vue'
import echarts, { type ECharts } from '@/utils/echarts'
import { resolveChartPalette, useChartThemeSync } from '@/utils/echarts-theme'
import { useDisplaySettingsStore } from '@/stores/display-settings'

export interface HoldingsBarDatum {
  name: string
  cost_basis: number
  current_value: number
}

const props = defineProps<{ data: HoldingsBarDatum[] }>()
const displaySettings = useDisplaySettingsStore()
const hasData = computed(() => (props.data || []).length > 0)

const chartRef = ref<HTMLDivElement | null>(null)
let chart: ECharts | null = null
let resizeObserver: ResizeObserver | null = null

function updateChart() {
  if (!chart) return
  const P = resolveChartPalette()
  const masked = !displaySettings.showAmount
  chart.setOption({
    tooltip: {
      trigger: 'axis',
      axisPointer: { type: 'shadow' },
      backgroundColor: P.surfaceCard,
      borderColor: P.border,
      textStyle: { color: P.textMain },
      valueFormatter: (v: any) => (masked ? '****' : String(v)),
    },
    legend: { bottom: 0, textStyle: { color: P.textMuted } },
    grid: { left: 16, right: 16, top: 32, bottom: 48, containLabel: true },
    xAxis: {
      type: 'category',
      data: props.data.map((d) => d.name),
      axisLabel: { color: P.textMuted, interval: 0, rotate: props.data.length > 4 ? 30 : 0 },
    },
    yAxis: {
      type: 'value',
      axisLabel: {
        color: P.textMuted,
        formatter: (v: number) => (masked ? '****' : String(v)),
      },
      splitLine: { lineStyle: { color: P.borderSubtle } },
    },
    series: [
      {
        name: '成本',
        type: 'bar',
        data: props.data.map((d) => d.cost_basis),
        itemStyle: { color: P.textMuted },
        barMaxWidth: 24,
      },
      {
        name: '市值',
        type: 'bar',
        data: props.data.map((d) => d.current_value),
        itemStyle: { color: P.primary },
        barMaxWidth: 24,
      },
    ],
  })
}

function handleResize() {
  chart?.resize()
}

function ensureChart() {
  if (!hasData.value || chart || !chartRef.value) return
  const el = chartRef.value
  if (!el.clientWidth || !el.clientHeight) return
  chart = echarts.init(el)
  updateChart()
}

function attachResizeObserver() {
  resizeObserver?.disconnect()
  resizeObserver = null
  if (!chartRef.value || typeof ResizeObserver === 'undefined') return
  resizeObserver = new ResizeObserver(() => {
    ensureChart()
    handleResize()
  })
  resizeObserver.observe(chartRef.value)
}

onMounted(() => {
  if (hasData.value) {
    ensureChart()
    attachResizeObserver()
  }
  window.addEventListener('resize', handleResize)
})

onUnmounted(() => {
  window.removeEventListener('resize', handleResize)
  resizeObserver?.disconnect()
  resizeObserver = null
  chart?.dispose()
  chart = null
})

watch(
  hasData,
  async (active) => {
    if (!active) {
      resizeObserver?.disconnect()
      resizeObserver = null
      chart?.dispose()
      chart = null
      return
    }
    await nextTick()
    ensureChart()
    attachResizeObserver()
    updateChart()
  },
)
watch(() => props.data, updateChart, { deep: true })
watch(() => displaySettings.showAmount, updateChart)
useChartThemeSync(updateChart)
</script>

<style scoped>
.holdings-bar-wrap {
  width: 100%;
  height: 280px;
  display: flex;
  align-items: center;
  justify-content: center;
}
.holdings-bar-chart {
  width: 100%;
  height: 100%;
}
</style>