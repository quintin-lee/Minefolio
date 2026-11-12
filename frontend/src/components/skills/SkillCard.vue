<template>
  <el-card class="skill-card" :class="{ 'is-disabled': skill.enabled === 0 }" shadow="hover">
    <div class="card-header">
      <div class="header-left">
        <div class="category-icon-box" :class="`cat-${skill.category}`">
          <Icon :icon="getCategoryIcon(skill.category)" class="skill-icon" />
        </div>
        <div class="title-meta">
          <div class="skill-name" :title="skill.name">{{ skill.name }}</div>
          <div class="badge-row">
            <el-tag size="small" :type="getCategoryTagType(skill.category)" effect="light">
              {{ getCategoryLabel(skill.category) }}
            </el-tag>
            <el-tag v-if="isL3" size="small" type="warning" effect="plain">
              <Icon icon="ph:terminal-window-duotone" class="mini-tag-icon" />
              L3 本地进程
            </el-tag>
            <el-tag v-else size="small" type="info" effect="plain">
              <Icon icon="ph:file-code-duotone" class="mini-tag-icon" />
              L2 指令规则
            </el-tag>
            <el-tag v-if="skill.license" size="small" effect="plain" class="license-tag">
              {{ skill.license }}
            </el-tag>
          </div>
        </div>
      </div>
      <div class="header-right">
        <el-switch
          :model-value="skill.enabled === 1"
          active-text=""
          inactive-text=""
          :loading="toggling"
          @change="handleToggle"
        />
      </div>
    </div>

    <div class="card-body">
      <p class="skill-desc" :title="skill.description">
        {{ skill.description || '暂无描述' }}
      </p>

      <div v-if="tagList.length > 0" class="skill-tags">
        <el-tag
          v-for="tag in tagList"
          :key="tag"
          size="small"
          class="tag-chip"
          effect="plain"
        >
          #{{ tag }}
        </el-tag>
      </div>
    </div>

    <div class="card-footer">
      <div class="meta-info">
        <span v-if="skill.instructions_l2" class="token-hint" :title="`指令长度: ${skill.instructions_l2.length} 字符`">
          <Icon icon="ph:text-t" />
          {{ skill.instructions_l2.length }} 字符
        </span>
        <span v-if="isL3" class="timeout-hint">
          <Icon icon="ph:timer" />
          {{ skill.timeout_ms || 5000 }}ms
        </span>
      </div>

      <div class="action-buttons">
        <el-tooltip content="调试运行技能" placement="top" :show-after="400">
          <el-button text circle size="small" @click="$emit('test', skill)">
            <Icon icon="ph:play-circle-duotone" class="action-icon play-icon" />
          </el-button>
        </el-tooltip>

        <el-tooltip content="导出为 SKILL.md" placement="top" :show-after="400">
          <el-button text circle size="small" @click="$emit('export', skill)">
            <Icon icon="ph:export-duotone" class="action-icon" />
          </el-button>
        </el-tooltip>

        <el-tooltip content="编辑技能配置" placement="top" :show-after="400">
          <el-button text circle size="small" @click="$emit('edit', skill)">
            <Icon icon="ph:pencil-simple-duotone" class="action-icon" />
          </el-button>
        </el-tooltip>

        <el-tooltip content="删除技能" placement="top" :show-after="400">
          <el-button text circle size="small" class="del-btn" @click="$emit('delete', skill)">
            <Icon icon="ph:trash-duotone" class="action-icon del-icon" />
          </el-button>
        </el-tooltip>
      </div>
    </div>
  </el-card>
</template>

<script setup lang="ts">
import { computed, ref } from 'vue'
import { Icon } from '@iconify/vue'
import type { AiSkill } from '@/types'

const props = defineProps<{
  skill: AiSkill
}>()

const emit = defineEmits<{
  (e: 'edit', skill: AiSkill): void
  (e: 'delete', skill: AiSkill): void
  (e: 'toggle', skill: AiSkill, enabled: boolean): void
  (e: 'test', skill: AiSkill): void
  (e: 'export', skill: AiSkill): void
}>()

const toggling = ref(false)

const isL3 = computed(() => {
  return !!(props.skill.exec_path && props.skill.exec_path.trim().length > 0)
})

const tagList = computed(() => {
  if (!props.skill.tags) return []
  return props.skill.tags
    .split(',')
    .map(t => t.trim())
    .filter(Boolean)
})

function handleToggle(val: string | number | boolean) {
  emit('toggle', props.skill, Boolean(val))
}

function getCategoryIcon(cat: string): string {
  switch (cat) {
    case 'finance':
      return 'ph:chart-line-up-duotone'
    case 'investment':
      return 'ph:trend-up-duotone'
    case 'expense':
      return 'ph:receipt-duotone'
    case 'tax':
      return 'ph:file-text-duotone'
    case 'risk':
      return 'ph:shield-warning-duotone'
    default:
      return 'ph:magic-wand-duotone'
  }
}

function getCategoryTagType(cat: string): 'primary' | 'success' | 'warning' | 'danger' | 'info' {
  switch (cat) {
    case 'finance':
      return 'success'
    case 'investment':
      return 'primary'
    case 'expense':
      return 'warning'
    case 'tax':
      return 'info'
    case 'risk':
      return 'danger'
    default:
      return 'info'
  }
}

function getCategoryLabel(cat: string): string {
  switch (cat) {
    case 'finance':
      return '财务审计'
    case 'investment':
      return '投资再平衡'
    case 'expense':
      return '支出风控'
    case 'tax':
      return '税务分类'
    case 'risk':
      return '风险管理'
    default:
      return cat || '综合'
  }
}
</script>

<style scoped>
.skill-card {
  display: flex;
  flex-direction: column;
  border-radius: 12px;
  transition: all 0.25s ease;
  border: 1px solid var(--el-border-color-lighter);
  background: var(--el-bg-color);
  overflow: hidden;
}

.skill-card:hover {
  transform: translateY(-2px);
  box-shadow: var(--el-box-shadow-light);
  border-color: var(--el-color-primary-light-5);
}

.skill-card.is-disabled {
  opacity: 0.68;
  filter: grayscale(0.2);
}

.card-header {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  gap: 12px;
  margin-bottom: 12px;
}

.header-left {
  display: flex;
  align-items: flex-start;
  gap: 12px;
  min-width: 0;
  flex: 1;
}

.category-icon-box {
  width: 40px;
  height: 40px;
  border-radius: 10px;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
  background: var(--el-fill-color-light);
  color: var(--el-color-primary);
  font-size: 22px;
}

.category-icon-box.cat-finance {
  background: rgba(16, 185, 129, 0.12);
  color: #10b981;
}

.category-icon-box.cat-investment {
  background: rgba(59, 130, 246, 0.12);
  color: #3b82f6;
}

.category-icon-box.cat-expense {
  background: rgba(245, 158, 11, 0.12);
  color: #f59e0b;
}

.category-icon-box.cat-tax {
  background: rgba(139, 92, 246, 0.12);
  color: #8b5cf6;
}

.category-icon-box.cat-risk {
  background: rgba(239, 68, 68, 0.12);
  color: #ef4444;
}

.title-meta {
  min-width: 0;
  flex: 1;
}

.skill-name {
  font-size: 15px;
  font-weight: 600;
  color: var(--el-text-color-primary);
  font-family: var(--font-mono, monospace);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  margin-bottom: 6px;
}

.badge-row {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 6px;
}

.mini-tag-icon {
  font-size: 12px;
  margin-right: 3px;
  vertical-align: -1px;
}

.license-tag {
  color: var(--el-text-color-secondary);
  border-color: var(--el-border-color-lighter);
}

.card-body {
  flex: 1;
  min-height: 72px;
  display: flex;
  flex-direction: column;
  justify-content: space-between;
}

.skill-desc {
  font-size: 13px;
  line-height: 1.5;
  color: var(--el-text-color-regular);
  display: -webkit-box;
  -webkit-line-clamp: 2;
  -webkit-box-orient: vertical;
  overflow: hidden;
  margin: 0 0 8px 0;
}

.skill-tags {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
  margin-bottom: 8px;
}

.tag-chip {
  font-size: 11px;
  padding: 0 6px;
  height: 20px;
  line-height: 18px;
  border-radius: 4px;
}

.card-footer {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding-top: 10px;
  border-top: 1px dashed var(--el-border-color-lighter);
  margin-top: 6px;
}

.meta-info {
  display: flex;
  align-items: center;
  gap: 10px;
  font-size: 12px;
  color: var(--el-text-color-secondary);
}

.meta-info span {
  display: inline-flex;
  align-items: center;
  gap: 3px;
}

.action-buttons {
  display: flex;
  align-items: center;
  gap: 2px;
}

.action-icon {
  font-size: 16px;
  color: var(--el-text-color-regular);
  transition: color 0.15s ease;
}

.action-buttons .el-button:hover .action-icon {
  color: var(--el-color-primary);
}

.action-buttons .el-button:hover .play-icon {
  color: #10b981;
}

.del-btn:hover .del-icon {
  color: var(--el-color-danger) !important;
}
</style>
