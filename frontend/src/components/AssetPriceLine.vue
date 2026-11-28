<template>
  <div class="asset-price-line-wrap">
    <div v-if="hasData" ref="chartRef" class="asset-price-line" />
    <el-empty v-else description="暂无净值走势数据" :image-size="64" />
  </div>
</template>

<script setup lang="ts">
import { computed, nextTick, ref, onMounted, onBeforeUnmount, watch } from 'vue'
import echarts, { type ECharts } from '@/utils/echarts'
import { resolveChartPalette, useChartThemeSync, withAlpha } from '@/utils/echarts-theme'
import { useDisplaySettingsStore } from '@/stores/display-settings'
import type { PriceHistoryItem } from '@/types'

const props = defineProps<{ data: PriceHistoryItem[] }>()
const hasData = computed(() => (props.data?.length ?? 0) > 0)

const chartRef = ref<HTMLElement | null>(null)
const displaySettings = useDisplaySettingsStore()
let chart: ECharts | null = null
let resizeObserver: ResizeObserver | null = null

function ensureChart() {
  if (!hasData.value || chart || !chartRef.value) return
  const el = chartRef.value
  if (!el.clientWidth || !el.clientHeight) return
  chart = echarts.init(el)
  render()
}

function render() {
  if (!chartRef.value) return
  if (!chart) {
    if (!chartRef.value.clientWidth || !chartRef.value.clientHeight) return
    chart = echarts.init(chartRef.value)
  }
  const P = resolveChartPalette()
  const masked = !displaySettings.showAmount
  const labels = props.data.map((p) => p.price_date)
  const values = props.data.map((p) => p.price)
  chart.setOption({
    grid: { left: 50, right: 20, top: 20, bottom: 30 },
    xAxis: {
      type: 'category',
      data: labels,
      axisLabel: { fontSize: 10, rotate: 30, color: P.textMuted },
      axisLine: { lineStyle: { color: P.borderSubtle } },
    },
    yAxis: {
      type: 'value',
      axisLabel: {
        fontSize: 10,
        color: P.textMuted,
        formatter: (v: number) => (masked ? '****' : String(v)),
      },
      splitLine: { lineStyle: { type: 'dashed', color: P.borderSubtle } },
    },
    tooltip: {
      trigger: 'axis',
      backgroundColor: P.surfaceCard,
      borderColor: P.border,
      textStyle: { color: P.textMain },
      valueFormatter: (v: any) => (masked ? '****' : String(v)),
    },
    series: [
      {
        name: '净值',
        type: 'line',
        data: values,
        smooth: true,
        lineStyle: { width: 2, color: P.primary },
        areaStyle: {
          color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
            { offset: 0, color: withAlpha(P.primary, 0.3) },
            { offset: 1, color: withAlpha(P.primary, 0.02) },
          ]),
        },
        symbol: 'none',
      },
    ],
  })
}

function attachResizeObserver() {
  resizeObserver?.disconnect()
  resizeObserver = null
  if (!chartRef.value) return
  resizeObserver = new ResizeObserver(() => {
    ensureChart()
    onResize()
  })
  resizeObserver.observe(chartRef.value)
}

onMounted(() => {
  if (hasData.value) {
    ensureChart()
    attachResizeObserver()
  }
  window.addEventListener('resize', onResize)
})

onBeforeUnmount(() => {
  window.removeEventListener('resize', onResize)
  resizeObserver?.disconnect()
  resizeObserver = null
  if (chart) {
    chart.dispose()
    chart = null
  }
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
    render()
  },
)
watch(() => props.data, render, { deep: true })
watch(() => displaySettings.showAmount, render)
useChartThemeSync(render)

function onResize() {
  chart?.resize()
}
</script>

<style scoped>
.asset-price-line-wrap {
  width: 100%;
  height: 100%;
  min-height: 180px;
  display: flex;
  align-items: center;
  justify-content: center;
}
.asset-price-line {
  width: 100%;
  height: 100%;
  min-height: 180px;
}
</style>
