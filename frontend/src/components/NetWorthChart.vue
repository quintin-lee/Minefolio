<template>
  <div ref="chartRef" style="height: 300px"></div>
</template>

<script setup lang="ts">
import { ref, onMounted, watch } from 'vue'
import * as echarts from 'echarts'

const props = defineProps<{ data: { date: string; net_worth: number }[] }>()
const chartRef = ref<HTMLElement>()
let chart: echarts.ECharts | null = null

onMounted(() => {
  chart = echarts.init(chartRef.value!)
  updateChart()
})

watch(() => props.data, updateChart, { deep: true })

function updateChart() {
  if (!chart || !props.data.length) return
  chart.setOption({
    tooltip: { trigger: 'axis', formatter: (p: any) => `${p[0].name}<br/>${p[0].seriesName}: ${p[0].value}` },
    grid: { left: 60, right: 20, top: 20, bottom: 30 },
    xAxis: { type: 'category', data: props.data.map((d) => d.date.slice(5)), axisLabel: { color: '#909399' } },
    yAxis: { type: 'value', axisLabel: { color: '#909399', formatter: (v: number) => (v >= 10000 ? `${(v / 10000).toFixed(1)}w` : v.toString()) } },
    series: [{ name: '净资产', type: 'line', data: props.data.map((d) => d.net_worth), smooth: true,
      areaStyle: { opacity: 0.15 }, itemStyle: { color: '#409eff' } }],
  })
}
</script>
