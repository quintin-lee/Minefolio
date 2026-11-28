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
          <el-button text type="primary" @click="openCategory(c)">编辑</el-button>
          <el-button text type="danger" @click="removeCategory(c)">删除</el-button>
        </span>
      </div>
      <div v-if="catTotal > catPageSize" class="mobile-pager">
        <el-button :disabled="catPage <= 1" @click="changeCatPage(catPage - 1)">上一页</el-button>
        <span class="pager-info">{{ catPage }} / {{ Math.max(1, Math.ceil(catTotal / catPageSize)) }}（共 {{ catTotal }}）</span>
        <el-button :disabled="catPage * catPageSize >= catTotal" @click="changeCatPage(catPage + 1)">下一页</el-button>
      </div>
    </template>

    <template v-else>
      <el-button type="primary" plain class="add-btn" @click="openTag()">新增标签</el-button>
      <div v-for="tag in tags" :key="tag.id" class="manage-row">
        <span class="row-main"><i class="dot" :style="{ background: tag.color || '#888' }" />{{ tag.name }}</span>
        <span class="row-actions">
          <el-button text type="primary" @click="openTag(tag)">编辑</el-button>
          <el-button text type="danger" @click="removeTag(tag)">删除</el-button>
        </span>
      </div>
      <div v-if="tagTotal > tagPageSize" class="mobile-pager">
        <el-button :disabled="tagPage <= 1" @click="changeTagPage(tagPage - 1)">上一页</el-button>
        <span class="pager-info">{{ tagPage }} / {{ Math.max(1, Math.ceil(tagTotal / tagPageSize)) }}（共 {{ tagTotal }}）</span>
        <el-button :disabled="tagPage * tagPageSize >= tagTotal" @click="changeTagPage(tagPage + 1)">下一页</el-button>
      </div>
    </template>

    <CategorySheet v-model="categorySheet" :record="editingCategory" :categories="allCategories.length ? allCategories : categories" @saved="onCategorySaved" />
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
import { fetchAllPages } from '@/utils/pagination'
import type { Category, Tag } from '@/types'

const categoryStore = useCategoryStore()
const tab = ref<'categories' | 'tags'>('categories')
const categories = ref<Category[]>([])
const allCategories = ref<Category[]>([])
const tags = ref<Tag[]>([])
const catPage = ref(1)
const catPageSize = 20
const catTotal = ref(0)
const tagPage = ref(1)
const tagPageSize = 20
const tagTotal = ref(0)
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
    const res: any = await categoriesApi.list({ page: catPage.value, page_size: catPageSize })
    const rawList: Category[] = Array.isArray(res) ? res : (Array.isArray(res?.list) ? res.list : [])
    const total = Array.isArray(res) ? res.length : (typeof res?.total === 'number' ? res.total : rawList.length)
    categories.value = flattenCategories(rawList)
    catTotal.value = total
    if (total <= rawList.length && catPage.value === 1) {
      allCategories.value = categories.value
    } else if (allCategories.value.length === 0) {
      fetchAllPages((p, ps) => categoriesApi.list({ page: p, page_size: ps }))
        .then(all => { allCategories.value = flattenCategories(all) })
        .catch(() => {})
    }
  } catch {
    try {
      const localCats = rowsFrom(query('SELECT * FROM categories WHERE __deleted = 0 ORDER BY sort_order ASC, id ASC')) as unknown as Category[]
      allCategories.value = localCats
      catTotal.value = localCats.length
      const start = (catPage.value - 1) * catPageSize
      categories.value = localCats.slice(start, start + catPageSize)
    } catch {
      categories.value = []
    }
  }
}

async function loadTags() {
  try {
    const res: any = await tagsApi.list({ page: tagPage.value, page_size: tagPageSize })
    const list: Tag[] = Array.isArray(res) ? res : (Array.isArray(res?.list) ? res.list : [])
    const total = Array.isArray(res) ? res.length : (typeof res?.total === 'number' ? res.total : list.length)
    tags.value = list
    tagTotal.value = total
  } catch {
    try {
      const localTags = rowsFrom(query('SELECT * FROM tags WHERE __deleted = 0 ORDER BY id ASC')) as unknown as Tag[]
      tagTotal.value = localTags.length
      const start = (tagPage.value - 1) * tagPageSize
      tags.value = localTags.slice(start, start + tagPageSize)
    } catch {
      tags.value = []
    }
  }
}

function changeCatPage(nextPage: number) {
  catPage.value = nextPage
  loadCategories()
}

function changeTagPage(nextPage: number) {
  tagPage.value = nextPage
  loadTags()
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
.mobile-pager { display: flex; align-items: center; justify-content: center; gap: 12px; padding: 12px 0; }
.pager-info { font-size: 12px; color: var(--mf-text-muted); }
</style>
