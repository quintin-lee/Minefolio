<template>
  <div class="holdings-pie-wrap">
    <div v-if="hasData" ref="chartRef" class="holdings-pie-chart" />
    <el-empty v-else description="暂无资产配置数据" :image-size="64" />
  </div>
</template>

<script setup lang="ts">
import { computed, nextTick, onMounted, onUnmounted, ref, watch } from 'vue'
import echarts, { type ECharts } from '@/utils/echarts'
import { resolveChartPalette, useChartThemeSync } from '@/utils/echarts-theme'
import { useDisplaySettingsStore } from '@/stores/display-settings'

export interface HoldingsPieDatum {
  name: string
  value: number
}

const props = defineProps<{ data: HoldingsPieDatum[] }>()
const displaySettings = useDisplaySettingsStore()
const hasData = computed(() => (props.data || []).some((d) => Number(d.value) > 0))

const chartRef = ref<HTMLDivElement | null>(null)
let chart: ECharts | null = null
let resizeObserver: ResizeObserver | null = null

const TYPE_LABELS: Record<string, string> = {
  stock: '股票',
  fund: '基金',
  bond: '债券',
  crypto: '加密货币',
}
const TYPE_COLORS: Record<string, string> = {
  stock: '#3b82f6',
  fund: '#10b981',
  bond: '#f59e0b',
  crypto: '#f43f5e',
}

function updateChart() {
  if (!chart) return
  const P = resolveChartPalette()
  const masked = !displaySettings.showAmount
  chart.setOption({
    tooltip: {
      trigger: 'item',
      formatter: (p: any) => `${p.name}: ${masked ? '****' : `¥${p.value}`} (${p.percent}%)`,
      backgroundColor: P.surfaceCard,
      borderColor: P.border,
      textStyle: { color: P.textMain },
    },
    legend: { bottom: 0, textStyle: { color: P.textMuted } },
    series: [
      {
        type: 'pie',
        radius: ['50%', '75%'],
        center: ['50%', '45%'],
        avoidLabelOverlap: true,
        itemStyle: { borderRadius: 4, borderColor: 'transparent', borderWidth: 2 },
        label: { show: false },
        data: props.data.map((d) => ({
          name: TYPE_LABELS[d.name] ?? d.name, // 仅展示用本地化
          value: d.value,
          itemStyle: { color: TYPE_COLORS[d.name] ?? '#94a3b8' }, // 颜色按原始 asset_type
        })),
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
.holdings-pie-wrap {
  width: 100%;
  height: 280px;
  display: flex;
  align-items: center;
  justify-content: center;
}
.holdings-pie-chart {
  width: 100%;
  height: 100%;
}
</style>