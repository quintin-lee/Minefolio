<template>
  <div ref="chartRef" style="height: 300px"></div>
</template>

<script setup lang="ts">
import { ref, onMounted, watch } from 'vue'
import * as echarts from 'echarts'

const props = defineProps<{ data: { category_name: string; value: number; pct: number }[] }>()
const chartRef = ref<HTMLElement>()
let chart: echarts.ECharts | null = null

const colors = ['#409eff', '#67c23a', '#e6a23c', '#f56c6c', '#909399', '#00d1b2', '#9c27b0', '#ff5722']

onMounted(() => {
  chart = echarts.init(chartRef.value!)
  updateChart()
})

watch(() => props.data, updateChart, { deep: true })

function updateChart() {
  if (!chart || !props.data.length) return
  chart.setOption({
    tooltip: { trigger: 'item', formatter: '{b}: {c} ({d}%)' },
    legend: { orient: 'vertical', right: 0, top: 'center', textStyle: { color: '#606266', fontSize: 12 } },
    series: [{
      type: 'pie', radius: ['40%', '70%'], center: ['40%', '50%'],
      data: props.data.map((d, i) => ({ name: d.category_name, value: d.value, pct: d.pct })),
      itemStyle: { borderRadius: 4, borderColor: '#fff', borderWidth: 2 },
      label: { show: false },
      color: colors,
    }],
  })
}
</script>
