<template>
  <div class="tag-picker">
    <div class="tag-picker__selected">
      <el-tag
        v-for="t in model"
        :key="t.id"
        :color="t.color || '#3b82f6'"
        closable
        size="small"
        effect="dark"
        class="premium-tag"
        @close="removeTag(t.id)"
      >
        {{ t.name }}
      </el-tag>
      <span v-if="model.length === 0" class="tag-picker__empty">未添加标签</span>
    </div>
    
    <div class="tag-picker__input-wrapper">
      <el-input
        v-model="input"
        placeholder="输入标签名后回车添加"
        size="small"
        class="premium-input"
        @keyup.enter="addTag"
        @focus="loadSuggestions()"
      >
        <template #append>
          <el-button class="add-btn" @click="addTag">添加</el-button>
        </template>
      </el-input>
    </div>
    
    <div v-if="suggestions.length" class="tag-picker__suggestions">
      <div
        v-for="s in suggestions"
        :key="s.id"
        class="suggestion-item"
        @click="addSuggestion(s)"
      >
        <span class="suggestion-dot" :style="{ backgroundColor: s.color || '#cbd5e1' }"></span>
        {{ s.name }}
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { tagsApi } from '@/api/tags'

export interface PickedTag {
  id: number
  name: string
  color?: string
}

const model = defineModel<PickedTag[]>({ default: () => [] })

const input = ref('')
const suggestions = ref<PickedTag[]>([])

async function loadSuggestions(q?: string) {
  try {
    const res = await tagsApi.suggestions(q)
    suggestions.value = res.map(t => ({ id: t.id, name: t.name, color: t.color || undefined }))
  } catch {
    suggestions.value = []
  }
}

function addTag() {
  const name = input.value.trim()
  if (!name) return
  if (!model.value.some(t => t.name === name)) {
    const existing = suggestions.value.find(t => t.name === name)
    if (existing) {
      model.value = [...model.value, existing]
    } else {
      model.value = [...model.value, { id: -Date.now(), name }]
    }
  }
  input.value = ''
}

function addSuggestion(s: PickedTag) {
  if (!model.value.some(t => t.id === s.id)) {
    model.value = [...model.value, s]
  }
}

function removeTag(id: number) {
  model.value = model.value.filter(t => t.id !== id)
}

onMounted(() => loadSuggestions())
</script>

<style scoped>
.tag-picker {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.tag-picker__selected {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
  min-height: 24px;
  align-items: center;
}

.premium-tag {
  border: none;
  border-radius: 6px;
  padding: 0 10px;
  font-weight: 500;
  box-shadow: 0 2px 4px rgba(0, 0, 0, 0.1);
}

.tag-picker__empty {
  color: #94a3b8;
  font-size: 13px;
  font-style: italic;
}

.tag-picker__input-wrapper {
  max-width: 300px;
}

.premium-input :deep(.el-input-group__append) {
  background-color: #f1f5f9;
  border-left: 0;
}

.add-btn {
  font-weight: 500;
  color: #3b82f6;
}

.tag-picker__suggestions {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  margin-top: 4px;
  padding: 8px;
  background: #f8fafc;
  border-radius: 8px;
  border: 1px dashed #e2e8f0;
}

.suggestion-item {
  cursor: pointer;
  font-size: 12px;
  padding: 4px 10px;
  border-radius: 12px;
  background: #ffffff;
  color: #475569;
  display: flex;
  align-items: center;
  gap: 6px;
  transition: all 0.2s;
  border: 1px solid #e2e8f0;
}

.suggestion-item:hover {
  background: #f1f5f9;
  border-color: #cbd5e1;
  color: #0f172a;
}

.suggestion-dot {
  width: 6px;
  height: 6px;
  border-radius: 50%;
}
</style>
