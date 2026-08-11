<template>
  <div class="tag-picker">
    <div class="tag-picker__selected">
      <el-tag
        v-for="t in model"
        :key="t.id"
        :color="t.color"
        closable
        size="small"
        class="tag-picker__tag"
        @close="removeTag(t.id)"
      >
        {{ t.name }}
      </el-tag>
      <span v-if="model.length === 0" class="tag-picker__empty">未添加标签</span>
    </div>
    <el-input
      v-model="input"
      placeholder="输入标签名后回车添加"
      size="small"
      class="tag-picker__input"
      @keyup.enter="addTag"
      @focus="loadSuggestions()"
    >
      <template #append>
        <el-button @click="addTag">添加</el-button>
      </template>
    </el-input>
    <div v-if="suggestions.length" class="tag-picker__suggestions">
      <el-link
        v-for="s in suggestions"
        :key="s.id"
        type="primary"
        :underline="false"
        class="tag-picker__suggestion"
        @click="addSuggestion(s)"
      >
        {{ s.name }}
      </el-link>
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
.tag-picker__selected { display: flex; flex-wrap: wrap; gap: 4px; margin-bottom: 8px; min-height: 24px; }
.tag-picker__tag { color: #fff; }
.tag-picker__empty { color: #909399; font-size: 12px; }
.tag-picker__input { max-width: 260px; }
.tag-picker__suggestions { margin-top: 6px; display: flex; flex-wrap: wrap; gap: 8px; }
.tag-picker__suggestion { cursor: pointer; }
</style>
