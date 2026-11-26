<template>
  <div class="sync-conflicts-card">
    <div class="card-title">同步冲突项（{{ conflicts.length }}）</div>
    <p class="card-hint">以下离线操作已暂停自动重试，请选择保留本地或采用服务端：</p>
    <div v-for="item in conflicts" :key="item.id" class="conflict-row">
      <div class="conflict-head">
        <span class="conflict-op">
          {{ item.table_name }} · {{ item.operation }} #{{ item.record_id }}
          <template v-if="isPlaceholder(item)">（占位 ID）</template>
        </span>
        <div class="conflict-actions">
          <el-button size="small" type="primary" plain @click="$emit('retry', item.id)">保留本地</el-button>
          <el-button size="small" type="danger" plain @click="$emit('discard', item.id)">采用服务端</el-button>
        </div>
      </div>
      <div class="conflict-diff">
        <div class="diff-col">
          <div class="diff-label">本地版本</div>
          <pre class="diff-body">{{ formatLocal(item) }}</pre>
        </div>
        <div class="diff-col">
          <div class="diff-label">服务端版本</div>
          <pre class="diff-body">{{ formatRemote(item) }}</pre>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import type { SyncQueueItem } from '@/types/mobile'

const props = defineProps<{ conflicts: SyncQueueItem[] }>()
defineEmits<{
  (e: 'retry', id: number): void
  (e: 'discard', id: number): void
}>()

function parseLocal(item: SyncQueueItem): Record<string, unknown> {
  try {
    return JSON.parse(item.payload) as Record<string, unknown>
  } catch {
    return {}
  }
}

function isPlaceholder(item: SyncQueueItem): boolean {
  return item.operation === 'create' && item.record_id > 1_000_000_000_000
}

function changedKeys(item: SyncQueueItem): string[] {
  const local = parseLocal(item)
  const remote = item.remote_payload
  if (!remote) return Object.keys(local).filter((k) => k !== 'id')
  const keys = new Set([...Object.keys(local), ...Object.keys(remote)])
  return [...keys].filter((k) => k !== 'id' && JSON.stringify(local[k]) !== JSON.stringify(remote[k]))
}

function formatValue(v: unknown): string {
  if (v === null || v === undefined) return '—'
  if (typeof v === 'object') return JSON.stringify(v)
  return String(v)
}

function formatLocal(item: SyncQueueItem): string {
  const local = parseLocal(item)
  const keys = changedKeys(item)
  if (keys.length === 0) return '（无差异字段）'
  return keys.map((k) => `${k}: ${formatValue(local[k])}`).join('\n')
}

function formatRemote(item: SyncQueueItem): string {
  const remote = item.remote_payload
  if (!remote) {
    return item.operation === 'create' ? '服务端无此记录' : '服务端版本不可读'
  }
  const keys = changedKeys(item)
  if (keys.length === 0) return '（无差异字段）'
  return keys.map((k) => `${k}: ${formatValue(remote[k])}`).join('\n')
}

// 保持 props 响应式引用被使用（模板中已直接遍历）
void props
</script>

<style scoped>
.sync-conflicts-card {
  margin-top: 12px;
  background: var(--mf-surface);
  border: 1px solid var(--mf-danger-border);
  border-radius: 12px;
  padding: 14px;
}
.card-title {
  font-size: 14px;
  font-weight: 600;
  color: var(--mf-text-primary);
}
.card-hint {
  margin: 6px 0 0;
  font-size: 12px;
  color: var(--mf-text-secondary);
}
.conflict-row {
  display: flex;
  flex-direction: column;
  gap: 8px;
  padding-top: 10px;
  margin-top: 10px;
  border-top: 1px solid var(--mf-border);
}
.conflict-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
}
.conflict-op {
  font-size: 13px;
  color: var(--mf-text-regular);
}
.conflict-actions {
  display: flex;
  gap: 6px;
}
.conflict-diff {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 8px;
}
.diff-label {
  font-size: 11px;
  color: var(--mf-text-secondary);
  margin-bottom: 4px;
}
.diff-body {
  margin: 0;
  padding: 8px;
  min-height: 36px;
  max-height: 120px;
  overflow: auto;
  background: var(--mf-bg);
  border: 1px solid var(--mf-border);
  border-radius: 8px;
  font-size: 11px;
  line-height: 1.5;
  white-space: pre-wrap;
  word-break: break-all;
}
</style>
