<template>
  <el-card class="asset-card" shadow="hover">
    <div class="asset-name">{{ asset.name }}</div>
    <div class="asset-value">
      {{ currency }}{{ asset.current_value.toFixed(2) }}
    </div>
  </el-card>
</template>

<script setup lang="ts">
interface Asset {
  name: string
  current_value: number
  currency?: string
}

const props = defineProps<{ asset: Asset }>()

const currency = props.asset.currency ?? '¥'
</script>

<style scoped>
.asset-card {
  text-align: center;
  padding: 8px 0;
}
.asset-name {
  font-size: 14px;
  color: #909399;
}
.asset-value {
  font-size: 22px;
  font-weight: bold;
  color: #303133;
  margin-top: 4px;
}
</style>
