<!-- AssetTrendLine.vue -->
<template>
  <div ref="chartRef" style="height: 250px"></div>
</template>
<script setup lang="ts">
import { ref, onMounted, watch } from 'vue'
import * as echarts from 'echarts'
const props = defineProps<{ data: { labels: string[]; net_worth: number[]; assets: number[]; liabilities: number[] } }>()
const chartRef = ref(); let chart: echarts.ECharts | null = null
onMounted(() => { chart = echarts.init(chartRef.value!); update() })
watch(() => props.data, update)
function update() { if (!chart || !props.data?.labels?.length) return
  chart.setOption({ tooltip: { trigger: 'axis' }, legend: { data: ['净资产', '总资产', '总负债'], top: 0 }, xAxis: { type: 'category', data: props.data.labels, axisLabel: { color: '#909399' } }, yAxis: { type: 'value', axisLabel: { formatter: (v: number) => v >= 10000 ? `${(v/10000).toFixed(1)}w` : String(v) } }, series: [
    { name: '净资产', type: 'line', data: props.data.net_worth, smooth: true, itemStyle: { color: '#409eff' }, areaStyle: { opacity: 0.1 } },
    { name: '总资产', type: 'line', data: props.data.assets, smooth: true, itemStyle: { color: '#67c23a' } },
    { name: '总负债', type: 'line', data: props.data.liabilities, smooth: true, itemStyle: { color: '#f56c6c' } },
  ]})
}
</script>
