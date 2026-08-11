<!-- ExpenseTrendBar.vue -->
<template>
  <div ref="chartRef" style="height: 250px"></div>
</template>
<script setup lang="ts">
import { ref, onMounted, watch } from 'vue'
import * as echarts from 'echarts'
const props = defineProps<{ data: { labels: string[]; income: number[]; expense: number[] } }>()
const chartRef = ref(); let chart: echarts.ECharts | null = null
onMounted(() => { chart = echarts.init(chartRef.value!); update() })
watch(() => props.data, update)
function update() { if (!chart) return
  chart.setOption({ tooltip: { trigger: 'axis', axisPointer: { type: 'shadow' } }, legend: { data: ['收入', '支出'], top: 0 }, xAxis: { type: 'category', data: props.data?.labels ?? [], axisLabel: { color: '#909399' } }, yAxis: { type: 'value', axisLabel: { formatter: (v: number) => v >= 10000 ? `${(v/10000).toFixed(1)}w` : String(v) } }, series: [{ name: '收入', type: 'bar', data: props.data?.income ?? [], itemStyle: { color: '#67c23a' } }, { name: '支出', type: 'bar', data: props.data?.expense ?? [], itemStyle: { color: '#f56c6c' } }] })
}
</script>
