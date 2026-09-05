<template>
  <div ref="chartRef" class="holdings-pie-chart" />
</template>

<script setup lang="ts">
import { onMounted, onUnmounted, ref, watch } from 'vue'
import * as echarts from 'echarts'

export interface HoldingsPieDatum {
  name: string
  value: number
}

const props = defineProps<{ data: HoldingsPieDatum[] }>()

const chartRef = ref<HTMLDivElement | null>(null)
let chart: echarts.ECharts | null = null
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
  chart.setOption({
    tooltip: {
      trigger: 'item',
      formatter: (p: any) => `${p.name}: ¥${p.value} (${p.percent}%)`,
      backgroundColor: 'var(--mf-surface-card)',
      borderColor: 'var(--mf-border)',
      textStyle: { color: 'var(--mf-text-main)' },
    },
    legend: { bottom: 0, textStyle: { color: 'var(--mf-text-muted)' } },
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
  if (chart || !chartRef.value) return
  const el = chartRef.value
  if (!el.clientWidth || !el.clientHeight) return
  chart = echarts.init(el)
  updateChart()
}
onMounted(() => {
  ensureChart()
  window.addEventListener('resize', handleResize)
  resizeObserver = new ResizeObserver(() => { ensureChart(); handleResize() })
  resizeObserver.observe(chartRef.value!)
})

onUnmounted(() => {
  window.removeEventListener('resize', handleResize)
  resizeObserver?.disconnect()
  chart?.dispose()
})

watch(() => props.data, updateChart, { deep: true })
</script>

<style scoped>
.holdings-pie-chart {
  width: 100%;
  height: 280px;
}
</style>