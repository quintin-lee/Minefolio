<!-- ExpenseCategoryPie.vue -->
<template>
  <div ref="chartRef" style="height: 250px; width: 100%;"></div>
</template>
<script setup lang="ts">
import { ref, onMounted, onUnmounted, watch } from 'vue'
import * as echarts from 'echarts'
const props = defineProps<{ data: { name: string; amount: number; pct: number }[] }>()
const chartRef = ref<HTMLElement>()
let chart: echarts.ECharts | null = null
let resizeObserver: ResizeObserver | null = null

const colors = ['#409eff','#67c23a','#e6a23c','#f56c6c','#00d1b2','#9c27b0','#ff5722','#909399']
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
  if (!chart || !props.data.length) return
  chart.setOption({ 
    animationDuration: 1000,
    tooltip: { 
      trigger: 'item', 
      backgroundColor: 'rgba(255, 255, 255, 0.95)',
      padding: [10, 15],
      textStyle: { color: '#303133' },
      borderColor: '#ebeef5',
      borderWidth: 1,
      shadowColor: 'rgba(0, 0, 0, 0.1)',
      shadowBlur: 10,
      formatter: (p: any) => {
        const val = new Intl.NumberFormat('zh-CN', { style: 'currency', currency: 'CNY' }).format(p.data.value)
        return `<div style="font-weight:bold;color:${p.color};margin-bottom:4px">${p.data.name}</div>
                <div style="color:#606266">金额: ${val}</div>
                <div style="color:#606266">占比: ${p.data.pct.toFixed(2)}%</div>`
      }
    }, 
    legend: { 
      orient: 'vertical', 
      right: '5%', 
      top: 'center', 
      textStyle: { color: '#606266', fontSize: 12 },
      icon: 'circle',
      itemWidth: 8,
      itemHeight: 8,
      itemGap: 12
    }, 
    series: [{ 
      type: 'pie', 
      radius: ['45%', '70%'], 
      center: ['35%', '50%'], 
      data: props.data.map(d => ({ name: d.name, value: d.amount, pct: d.pct })), 
      avoidLabelOverlap: false,
      itemStyle: { borderRadius: 6, borderColor: '#fff', borderWidth: 2 }, 
      label: { show: false, position: 'center' },
      emphasis: {
        label: {
          show: true,
          fontSize: 14,
          fontWeight: 'bold',
          formatter: '{b}\n{d}%'
        },
        itemStyle: {
          shadowBlur: 10,
          shadowOffsetX: 0,
          shadowColor: 'rgba(0, 0, 0, 0.2)'
        }
      },
      labelLine: { show: false },
      color: colors 
    }] 
  })
}
</script>
