<!-- AssetTrendLine.vue -->
<template>
  <div ref="chartRef" style="height: 250px; width: 100%;"></div>
</template>
<script setup lang="ts">
import { ref, onMounted, onUnmounted, watch } from 'vue'
import * as echarts from 'echarts'
const props = defineProps<{ data: { labels: string[]; net_worth: number[]; assets: number[]; liabilities: number[] } }>()
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
      backgroundColor: 'rgba(255, 255, 255, 0.95)',
      padding: [10, 15],
      textStyle: { color: '#303133' },
      borderColor: '#ebeef5',
      borderWidth: 1,
      shadowColor: 'rgba(0, 0, 0, 0.1)',
      shadowBlur: 10,
    }, 
    legend: { 
      data: ['净资产', '总资产', '总负债'], 
      top: 0,
      icon: 'circle',
      itemWidth: 10,
      itemHeight: 10,
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
        name: '净资产', 
        type: 'line', 
        data: props.data.net_worth, 
        smooth: 0.4, 
        symbol: 'circle',
        symbolSize: 6,
        showSymbol: false,
        itemStyle: { color: '#409eff', borderWidth: 2 }, 
        lineStyle: { width: 3, shadowColor: 'rgba(64,158,255,0.2)', shadowBlur: 10 },
        areaStyle: { 
          color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
            { offset: 0, color: 'rgba(64,158,255,0.3)' },
            { offset: 1, color: 'rgba(64,158,255,0.05)' }
          ])
        } 
      },
      { 
        name: '总资产', 
        type: 'line', 
        data: props.data.assets, 
        smooth: 0.4, 
        symbol: 'circle',
        symbolSize: 6,
        showSymbol: false,
        itemStyle: { color: '#67c23a', borderWidth: 2 },
        lineStyle: { width: 2 }
      },
      { 
        name: '总负债', 
        type: 'line', 
        data: props.data.liabilities, 
        smooth: 0.4, 
        symbol: 'circle',
        symbolSize: 6,
        showSymbol: false,
        itemStyle: { color: '#f56c6c', borderWidth: 2 },
        lineStyle: { width: 2 }
      },
    ]
  })
}
</script>
