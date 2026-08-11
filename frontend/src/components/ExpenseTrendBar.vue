<!-- ExpenseTrendBar.vue -->
<template>
  <div ref="chartRef" style="height: 250px; width: 100%;"></div>
</template>
<script setup lang="ts">
import { ref, onMounted, onUnmounted, watch } from 'vue'
import * as echarts from 'echarts'
const props = defineProps<{ data: { labels: string[]; income: number[]; expense: number[] } }>()
const chartRef = ref<HTMLElement>()
let chart: echarts.ECharts | null = null
let resizeObserver: ResizeObserver | null = null

onMounted(() => { 
  chart = echarts.init(chartRef.value!)
  update() 
  window.addEventListener('resize', handleResize)
  if (chartRef.value) {
    resizeObserver = new ResizeObserver(() => handleResize())
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
watch(() => props.data, update, { deep: true })
function update() { 
  if (!chart || !props.data?.labels?.length) return
  chart.setOption({ 
    animationDuration: 1000,
    tooltip: { 
      trigger: 'axis', 
      axisPointer: { type: 'shadow', shadowStyle: { color: 'rgba(0,0,0,0.03)' } },
      backgroundColor: 'rgba(255, 255, 255, 0.95)',
      padding: [10, 15],
      textStyle: { color: '#303133' },
      borderColor: '#ebeef5',
      borderWidth: 1,
      shadowColor: 'rgba(0, 0, 0, 0.1)',
      shadowBlur: 10,
    }, 
    legend: { 
      data: ['收入', '支出'], 
      top: 0,
      icon: 'roundRect',
      itemWidth: 16,
      itemHeight: 8,
      textStyle: { color: '#606266' }
    }, 
    grid: { left: 50, right: 20, top: 40, bottom: 20, containLabel: true },
    xAxis: { 
      type: 'category', 
      data: props.data.labels, 
      axisLine: { lineStyle: { color: '#ebeef5' } },
      axisLabel: { color: '#909399', margin: 12 },
      axisTick: { show: false } 
    }, 
    yAxis: { 
      type: 'value', 
      splitLine: { lineStyle: { type: 'dashed', color: '#ebeef5' } },
      axisLabel: { 
        color: '#909399',
        formatter: (v: number) => v >= 10000 ? `${(v/10000).toFixed(1)}w` : String(v) 
      } 
    }, 
    series: [
      { 
        name: '收入', 
        type: 'bar', 
        barMaxWidth: 30,
        data: props.data.income, 
        itemStyle: { 
          borderRadius: [4, 4, 0, 0],
          color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
            { offset: 0, color: '#85ce61' },
            { offset: 1, color: '#67c23a' }
          ]) 
        } 
      }, 
      { 
        name: '支出', 
        type: 'bar', 
        barMaxWidth: 30,
        data: props.data.expense, 
        itemStyle: { 
          borderRadius: [4, 4, 0, 0],
          color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
            { offset: 0, color: '#f78989' },
            { offset: 1, color: '#f56c6c' }
          ]) 
        } 
      }
    ] 
  })
}
</script>
