<template>
  <span
    v-if="!isOnline"
    class="net-status-pill is-offline"
    title="当前处于离线模式，核心账目将暂存本地待联网同步"
  >离线模式</span>
  <span
    v-else-if="pendingCount > 0 || conflictCount > 0"
    class="net-status-pill is-pending"
    title="点击立即同步本地离线变更"
    @click="$emit('click')"
  >
    <template v-if="pendingCount > 0">待同步 {{ pendingCount }}</template>
    <template v-else>同步冲突 {{ conflictCount }}</template>
  </span>
</template>

<script setup lang="ts">
defineProps<{
  isOnline: boolean
  pendingCount: number
  conflictCount: number
  syncing: boolean
}>()
defineEmits<{ (e: 'click'): void }>()
</script>

<style scoped>
.net-status-pill {
  font-size: 11px;
  font-weight: 600;
  padding: 2px 8px;
  border-radius: 999px;
  line-height: 1.5;
  white-space: nowrap;
}
.net-status-pill.is-offline {
  background: rgba(239, 68, 68, 0.14);
  color: #ef4444;
  border: 1px solid rgba(239, 68, 68, 0.35);
}
.net-status-pill.is-pending {
  background: rgba(245, 158, 11, 0.14);
  color: #d97706;
  border: 1px solid rgba(245, 158, 11, 0.35);
  cursor: pointer;
}
</style>
