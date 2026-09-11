<template>
  <div class="donut-chart-container">
    <div ref="chartRef" class="chart-canvas"></div>
    <div class="donut-center-metric">
      <div class="center-title">{{ activeItem ? activeItem.name : '配置总计' }}</div>
      <div class="center-val tabular-nums">{{ formatCurrency(activeItem ? activeItem.value : totalValue) }}</div>
      <div v-if="activeItem" class="center-pct tabular-nums">{{ activeItem.pct.toFixed(1) }}%</div>
      <div v-else class="center-sub muted-text">100%</div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted, watch, computed } from 'vue'
import echarts, { type ECharts } from '@/utils/echarts'
import { resolveChartPalette, useChartThemeSync, modernTooltipConfig } from '@/utils/echarts-theme'
import { formatCurrency } from '@/utils/format'

const props = defineProps<{ data: { category_name: string; value: number; pct: number }[] }>()
const chartRef = ref<HTMLElement>()
const activeItem = ref<{ name: string; value: number; pct: number } | null>(null)
let chart: ECharts | null = null
let resizeObserver: ResizeObserver | null = null

const colors = ['#3b82f6', '#8b5cf6', '#10b981', '#f59e0b', '#ec4899', '#06b6d4', '#6366f1', '#14b8a6']

const totalValue = computed(() => {
  return (props.data || []).reduce((acc, cur) => acc + (cur.value || 0), 0)
})

function ensureChart() {
  if (chart || !chartRef.value) return
  const el = chartRef.value
  if (!el.clientWidth || !el.clientHeight) return
  chart = echarts.init(el)
  chart.on('mouseover', 'series.pie', (params: any) => {
    if (params.data) {
      activeItem.value = {
        name: params.data.name,
        value: params.data.value,
        pct: params.data.pct
      }
    }
  })
  chart.on('mouseout', 'series.pie', () => {
    activeItem.value = null
  })
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
    animationDuration: 800,
    tooltip: {
      ...modernTooltipConfig(P),
      trigger: 'item',
      formatter: (p: any) => {
        const val = formatCurrency(p.data.value)
        return `<div style="font-weight:700;color:${p.color};margin-bottom:4px;font-family:var(--mf-font-mono)">${p.data.name}</div>
                <div style="color:${P.textRegular};font-family:var(--mf-font-mono)">金额: ${val}</div>
                <div style="color:${P.textMuted};font-family:var(--mf-font-mono)">占比: ${p.data.pct.toFixed(1)}%</div>`
      }
    },
    legend: {
      orient: 'vertical',
      right: '2%',
      top: 'middle',
      textStyle: { color: P.textMuted, fontSize: 11, fontFamily: 'var(--mf-font-mono)' },
      icon: 'circle',
      itemWidth: 8,
      itemHeight: 8,
      itemGap: 12
    },
    series: [{
      type: 'pie',
      radius: ['58%', '76%'],
      center: ['35%', '50%'],
      avoidLabelOverlap: true,
      itemStyle: {
        borderRadius: 6,
        borderColor: P.surfaceCard,
        borderWidth: 3
      },
      label: { show: false },
      labelLine: { show: false },
      emphasis: {
        scale: true,
        scaleSize: 6,
        itemStyle: {
          shadowBlur: 14,
          shadowColor: 'rgba(0, 0, 0, 0.4)'
        }
      },
      data: props.data.map((d, i) => ({
        name: d.category_name,
        value: d.value,
        pct: d.pct,
        itemStyle: { color: colors[i % colors.length] }
      }))
    }]
  })
}
</script>

<style scoped>
.donut-chart-container {
  position: relative;
  width: 100%;
  height: 290px;
}
.chart-canvas {
  width: 100%;
  height: 100%;
}
.donut-center-metric {
  position: absolute;
  left: 35%;
  top: 50%;
  transform: translate(-50%, -50%);
  text-align: center;
  pointer-events: none;
  width: 110px;
}
.center-title {
  font-size: 11px;
  color: var(--mf-text-muted);
  font-weight: 500;
  margin-bottom: 2px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.center-val {
  font-size: 15px;
  font-weight: 700;
  color: var(--mf-text-main);
  line-height: 1.2;
}
.center-pct {
  font-size: 12px;
  font-weight: 600;
  color: var(--mf-primary);
  margin-top: 2px;
}
.center-sub {
  font-size: 11px;
  color: var(--mf-text-placeholder);
  margin-top: 2px;
}
</style>
