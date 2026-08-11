<template>
  <div class="categories-page">
    <el-row :gutter="16">
      <el-col :span="8">
        <el-card>
          <template #header>
            <div class="header">
              <span>分类树</span>
              <el-button type="primary" size="small" @click="openDialog(null)">
                <el-icon><Plus /></el-icon> 一级分类
              </el-button>
            </div>
          </template>
          <el-tree :data="treeData" :props="{ label: 'name', children: 'children' }"
            node-key="id" default-expand-all
            :expand-on-click-node="false"
            @node-click="onNodeClick">
            <template #default="{ node, data }">
              <span class="tree-node">
                <span>{{ node.label }}</span>
                <span class="tree-actions">
                  <el-button link size="small" @click.stop="openDialog(data)">编辑</el-button>
                  <el-button link size="small" type="danger" @click.stop="handleDelete(data)">删除</el-button>
                </span>
              </span>
            </template>
          </el-tree>
        </el-card>
      </el-col>
      <el-col :span="16">
        <el-card>
          <template #header><span>分类列表</span></template>
          <el-table :data="flatCategories" stripe>
            <el-table-column prop="name" label="名称" />
            <el-table-column prop="parent_name" label="上级分类" />
            <el-table-column prop="asset_type" label="类型" width="100">
              <template #default="{ row }">{{ assetTypeLabel(row.asset_type) }}</template>
            </el-table-column>
            <el-table-column prop="currency" label="币种" width="80" />
            <el-table-column label="操作" width="120">
              <template #default="{ row }">
                <el-button link type="primary" @click="openDialog(row)">编辑</el-button>
                <el-button link type="danger" @click="handleDelete(row)">删除</el-button>
              </template>
            </el-table-column>
          </el-table>
        </el-card>
      </el-col>
    </el-row>

    <el-dialog v-model="dialogVisible" :title="editingId ? '编辑分类' : '新增分类'" width="480px">
      <el-form ref="formRef" :model="form" :rules="rules" label-width="100px">
        <el-form-item label="分类名称" prop="name">
          <el-input v-model="form.name" />
        </el-form-item>
        <el-form-item label="上级分类">
          <el-cascader v-model="form._parentPath" :options="parentOptions" :props="{ value: 'id', label: 'name', checkStrictly: true }" placeholder="无（一级分类）" style="width: 100%" @change="onParentChange" />
        </el-form-item>
        <el-form-item label="资产类型" prop="asset_type">
          <el-select v-model="form.asset_type" style="width: 100%">
            <el-option v-for="t in assetTypes" :key="t.value" :label="t.label" :value="t.value" />
          </el-select>
        </el-form-item>
        <el-form-item label="币种">
          <el-select v-model="form.currency" style="width: 100%">
            <el-option label="CNY" value="CNY" /><el-option label="USD" value="USD" /><el-option label="EUR" value="EUR" />
          </el-select>
        </el-form-item>
        <el-form-item label="排序">
          <el-input-number v-model="form.sort_order" :min="0" />
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="dialogVisible = false">取消</el-button>
        <el-button type="primary" :loading="saving" @click="handleSubmit">保存</el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, computed, onMounted } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { categoriesApi } from '@/api/categories'
import { useCategoryStore } from '@/stores/category'
import type { Category } from '@/types'

const categoryStore = useCategoryStore()

const categories = ref<Category[]>([])
const treeData = computed(() => buildTree(categories.value))
const flatCategories = ref<Category[]>([])
const dialogVisible = ref(false)
const editingId = ref<number | null>(null)
const saving = ref(false)
const formRef = ref()

const assetTypes = [
  { label: '现金', value: 'cash' }, { label: '股票', value: 'stock' },
  { label: '基金', value: 'fund' }, { label: '债券', value: 'bond' },
  { label: '加密货币', value: 'crypto' }, { label: '房产', value: 'real_estate' },
  { label: '车辆', value: 'vehicle' }, { label: '其他资产', value: 'other_asset' },
  { label: '贷款', value: 'loan' }, { label: '信用卡', value: 'credit_card' },
  { label: '其他负债', value: 'other_liability' },
]

const form = reactive({ name: '', asset_type: 'cash', currency: 'CNY', sort_order: 0, parent_id: null as number | null, _parentPath: [] as number[], _hasChildren: false })
const rules = { name: [{ required: true }], asset_type: [{ required: true }] }

function buildTree(list: Category[]): Category[] {
  const map = new Map<number, Category>()
  list.forEach(c => map.set(c.id, { ...c }))
  const roots: Category[] = []
  list.forEach(c => {
    if (c.parent_id === null || c.parent_id === undefined) roots.push(c)
    else { const p = map.get(c.parent_id); if (p) (p.children ||= []).push(c) }
  })
  return roots
}

function flatten(list: Category[]): Category[] {
  const out: Category[] = []
  const walk = (nodes: Category[]) => {
    for (const node of nodes) {
      out.push(node)
      if (node.children?.length) walk(node.children)
    }
  }
  walk(list)
  return out
}

function onNodeClick(data: any) { /* select for editing */ }
function assetTypeLabel(t: string) { return assetTypes.find(x => x.value === t)?.label || t }
const parentOptions = computed(() => categories.value.filter(c => !c.children?.length || true))

async function loadData() {
  const data = await categoryStore.loadCategories(true)
  categories.value = data
  flatCategories.value = flatten(data)
}

function openDialog(cat?: any) {
  editingId.value = cat?.id ?? null
  Object.assign(form, cat ? { name: cat.name, asset_type: cat.asset_type, currency: cat.currency, sort_order: cat.sort_order, parent_id: cat.parent_id, _parentPath: cat.parent_id ? [cat.parent_id] : [], _hasChildren: !!cat.children }
    : { name: '', asset_type: 'cash', currency: 'CNY', sort_order: 0, parent_id: null, _parentPath: [], _hasChildren: false })
  dialogVisible.value = true
}

function onParentChange(val: any) { form.parent_id = (val as number[])?.[(val as number[]).length - 1] ?? null }

async function handleSubmit() {
  await formRef.value.validate(async (valid: boolean) => {
    if (!valid) return
    saving.value = true
    try {
      const data = { name: form.name, asset_type: form.asset_type, currency: form.currency, sort_order: form.sort_order, parent_id: form.parent_id }
      if (editingId.value) await categoriesApi.update(editingId.value, data)
      else await categoriesApi.create(data)
      ElMessage.success('保存成功')
      dialogVisible.value = false
      loadData()
    } finally { saving.value = false }
  })
}

async function handleDelete(cat: any) {
  await ElMessageBox.confirm('确定删除该分类吗？', '提示', { type: 'warning' })
  try {
    await categoriesApi.delete(cat.id)
    ElMessage.success('删除成功')
    loadData()
  } catch (e: any) {
    if (e?.response?.data?.code === 2001) ElMessage.warning('分类下有子分类，请先删除子分类')
    else ElMessage.error('删除失败')
  }
}

onMounted(loadData)
</script>

<style scoped>
.categories-page { }
.header { display: flex; justify-content: space-between; align-items: center; }
.tree-node { display: flex; align-items: center; justify-content: space-between; width: 100%; }
.tree-actions { opacity: 0; transition: opacity 0.2s; }
.el-tree-node__content:hover .tree-actions { opacity: 1; }
</style>
