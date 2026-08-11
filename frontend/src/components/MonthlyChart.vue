<template>
  <div ref="chartRef" style="height: 280px"></div>
</template>

<script setup lang="ts">
import { ref, onMounted, watch } from 'vue'
import * as echarts from 'echarts'

const props = defineProps<{ data: { total_income: number; total_expense: number } | null }>()
const chartRef = ref<HTMLElement>()
let chart: echarts.ECharts | null = null

onMounted(() => {
  chart = echarts.init(chartRef.value!)
  updateChart()
})

watch(() => props.data, updateChart)

function updateChart() {
  if (!chart) return
  const d = props.data
  chart.setOption({
    tooltip: { trigger: 'axis', axisPointer: { type: 'shadow' } },
    legend: { data: ['收入', '支出'], top: 0 },
    grid: { left: 60, right: 20, top: 40, bottom: 20 },
    xAxis: { type: 'category', data: ['本月'] },
    yAxis: { type: 'value', axisLabel: { formatter: (v: number) => (v >= 10000 ? `${(v / 10000).toFixed(1)}w` : v.toString()) } },
    series: [
      { name: '收入', type: 'bar', data: [d?.total_income ?? 0], itemStyle: { color: '#67c23a' } },
      { name: '支出', type: 'bar', data: [d?.total_expense ?? 0], itemStyle: { color: '#f56c6c' } },
    ],
  })
}
</script>
