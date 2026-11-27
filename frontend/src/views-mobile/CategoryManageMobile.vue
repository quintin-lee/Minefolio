<template>
  <div class="category-manage-mobile">
    <div class="page-header"><h2>分类与标签</h2></div>

    <div class="segmented">
      <button :class="{ active: tab === 'categories' }" @click="tab = 'categories'">分类</button>
      <button :class="{ active: tab === 'tags' }" @click="tab = 'tags'">标签</button>
    </div>

    <template v-if="tab === 'categories'">
      <el-button type="primary" plain class="add-btn" @click="openCategory()">新增分类</el-button>
      <div v-for="c in categories" :key="c.id" class="manage-row" :style="{ paddingLeft: `${c.parent_id ? 24 : 12}px` }">
        <span class="row-main">{{ c.name }}<small v-if="c.parent_id"> · 子分类</small></span>
        <span class="row-actions">
          <el-button size="small" text type="primary" @click="openCategory(c)">编辑</el-button>
          <el-button size="small" text type="danger" @click="removeCategory(c)">删除</el-button>
        </span>
      </div>
    </template>

    <template v-else>
      <el-button type="primary" plain class="add-btn" @click="openTag()">新增标签</el-button>
      <div v-for="tag in tags" :key="tag.id" class="manage-row">
        <span class="row-main"><i class="dot" :style="{ background: tag.color || '#888' }" />{{ tag.name }}</span>
        <span class="row-actions">
          <el-button size="small" text type="primary" @click="openTag(tag)">编辑</el-button>
          <el-button size="small" text type="danger" @click="removeTag(tag)">删除</el-button>
        </span>
      </div>
    </template>

    <CategorySheet v-model="categorySheet" :record="editingCategory" :categories="categories" @saved="onCategorySaved" />
    <TagSheet v-model="tagSheet" :record="editingTag" @saved="load" />
  </div>
</template>

<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { query, rowsFrom, run, persist } from '@/db/local'
import { categoriesApi } from '@/api/categories'
import { tagsApi } from '@/api/tags'
import { useCategoryStore } from '@/stores/category'
import { offlineApi } from '@/utils/offline-http'
import CategorySheet from '@/components/mobile/CategorySheet.vue'
import TagSheet from '@/components/mobile/TagSheet.vue'
import type { Category, Tag } from '@/types'

const categoryStore = useCategoryStore()
const tab = ref<'categories' | 'tags'>('categories')
const categories = ref<Category[]>([])
const tags = ref<Tag[]>([])
const categorySheet = ref(false)
const tagSheet = ref(false)
const editingCategory = ref<Category | null>(null)
const editingTag = ref<Tag | null>(null)

function flattenCategories(list: Category[]): Category[] {
  const out: Category[] = []
  const seen = new Set<number>()
  const walk = (nodes: Category[]) => {
    for (const node of nodes) {
      const id = Number(node.id)
      if (!seen.has(id)) {
        seen.add(id)
        out.push(node)
      }
      if (Array.isArray(node.children) && node.children.length > 0) {
        walk(node.children)
      }
    }
  }
  walk(list)
  return out
}

async function loadCategories() {
  try {
    const res = await categoriesApi.list()
    categories.value = flattenCategories(Array.isArray(res) ? res : [])
  } catch {
    try {
      categories.value = rowsFrom(query('SELECT * FROM categories WHERE __deleted = 0 ORDER BY sort_order ASC, id ASC')) as unknown as Category[]
    } catch {
      categories.value = []
    }
  }
}

async function loadTags() {
  try {
    const res = await tagsApi.list()
    tags.value = Array.isArray(res) ? res : []
  } catch {
    try {
      tags.value = rowsFrom(query('SELECT * FROM tags WHERE __deleted = 0 ORDER BY id ASC')) as unknown as Tag[]
    } catch {
      tags.value = []
    }
  }
}

async function load() {
  await Promise.all([loadCategories(), loadTags()])
}

async function onCategorySaved() {
  categoryStore.invalidate()
  await load()
}

function openCategory(record: Category | null = null) {
  editingCategory.value = record
  categorySheet.value = true
}
function openTag(record: Tag | null = null) {
  editingTag.value = record
  tagSheet.value = true
}

async function removeCategory(c: Category) {
  try {
    await ElMessageBox.confirm(`删除分类「${c.name}」？`, '确认删除', { type: 'warning' })
    await offlineApi.delete(`/categories/${c.id}`)
    categoryStore.invalidate()
    await load()
  } catch (err: any) {
    if (err !== 'cancel' && err !== 'close') ElMessage.error(String(err?.response?.data?.message || err?.message || '删除失败'))
  }
}

async function removeTag(tag: Tag) {
  try {
    await ElMessageBox.confirm(`删除标签「${tag.name}」？`, '确认删除', { type: 'warning' })
    await offlineApi.delete(`/tags/${tag.id}`)
    try {
      run('UPDATE expense_tags SET __deleted = 1 WHERE tag_id = ?', [tag.id])
      persist()
    } catch {
      // localDb 不可用或未初始化时忽略本地级联更新
    }
    await load()
  } catch (err: any) {
    if (err !== 'cancel' && err !== 'close') ElMessage.error(String(err?.response?.data?.message || err?.message || '删除失败'))
  }
}

onMounted(load)
</script>

<style scoped>
.category-manage-mobile { padding: 12px; }
.page-header h2 { font-size: 18px; margin: 0 0 12px; }
.segmented { display: flex; gap: 8px; margin-bottom: 12px; }
.segmented button { flex: 1; padding: 8px; border: 1px solid var(--mf-border); border-radius: 8px; background: transparent; color: var(--mf-text-regular); }
.segmented button.active { color: var(--mf-primary); border-color: var(--mf-primary); }
.add-btn { width: 100%; margin-bottom: 12px; }
.manage-row { display: flex; align-items: center; justify-content: space-between; gap: 8px; padding: 10px 12px; border-bottom: 1px solid var(--mf-border); }
.row-main { display: flex; align-items: center; gap: 6px; min-width: 0; font-size: 13px; }
.row-main small { color: var(--mf-text-secondary); }
.row-actions { display: flex; flex-shrink: 0; }
.dot { width: 8px; height: 8px; border-radius: 50%; display: inline-block; }
</style>
