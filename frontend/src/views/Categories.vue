<template>
  <div class="categories-page">
    <div class="page-header">
      <div class="header-title">
        <div class="title-accent"></div>
        <h2>分类管理</h2>
      </div>
      <el-button type="primary" class="action-btn" @click="openDialog(null)">
        <el-icon><Plus /></el-icon> 新增分类
      </el-button>
    </div>

    <el-row :gutter="24">
      <el-col :span="8">
        <div class="panel-container tree-panel-container">
          <div class="panel-header">
            <h3>分类结构</h3>
          </div>
          <el-tree :data="treeData" :props="{ label: 'name', children: 'children' }"
            node-key="id" default-expand-all class="premium-tree"
            :expand-on-click-node="false"
            @node-click="onNodeClick">
            <template #default="{ node, data }">
              <div class="tree-node-wrapper">
                <div class="tree-node-content">
                  <span class="node-icon" v-if="!data.children || data.children.length === 0">·</span>
                  <span class="node-icon folder" v-else>📁</span>
                  <span class="node-label">{{ node.label }}</span>
                </div>
                <div class="tree-actions">
                  <el-button link size="small" type="primary" @click.stop="openDialog(data)">编辑</el-button>
                  <el-button link size="small" type="danger" @click.stop="handleDelete(data)">删除</el-button>
                </div>
              </div>
            </template>
          </el-tree>
        </div>
      </el-col>
      <el-col :span="16">
        <div class="panel-container">
          <div class="panel-header">
            <h3>分类列表</h3>
          </div>
          <el-table :data="flatCategories" class="premium-table" row-class-name="premium-row" header-cell-class-name="premium-header">
            <el-table-column prop="name" label="名称" min-width="140" />
            <el-table-column prop="parent_name" label="上级分类" min-width="120">
              <template #default="{ row }">
                <span class="text-muted">{{ row.parent_name || '-' }}</span>
              </template>
            </el-table-column>
            <el-table-column prop="asset_type" label="类型" width="140">
              <template #default="{ row }">
                <div class="type-cell">
                  <span :class="['status-dot', row.asset_type]"></span>
                  {{ assetTypeLabel(row.asset_type) }}
                </div>
              </template>
            </el-table-column>
            <el-table-column prop="currency" label="币种" width="100">
              <template #default="{ row }">
                <el-tag size="small" class="currency-tag" effect="plain">{{ row.currency }}</el-tag>
              </template>
            </el-table-column>
            <el-table-column label="操作" width="120" align="center">
              <template #default="{ row }">
                <div class="action-buttons">
                  <el-button link type="primary" @click="openDialog(row)">编辑</el-button>
                  <el-button link type="danger" @click="handleDelete(row)">删除</el-button>
                </div>
              </template>
            </el-table-column>
          </el-table>
        </div>
      </el-col>
    </el-row>

    <el-dialog v-model="dialogVisible" :title="editingId ? '编辑分类' : '新增分类'" width="480px" class="premium-dialog" :show-close="false">
      <el-form ref="formRef" :model="form" :rules="rules" label-width="90px" class="premium-form">
        <el-form-item label="分类名称" prop="name">
          <el-input v-model="form.name" placeholder="输入分类名称" />
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
          <el-input-number v-model="form.sort_order" :min="0" style="width: 100%" :controls="false" />
        </el-form-item>
      </el-form>
      <template #footer>
        <div class="dialog-footer">
          <el-button class="cancel-btn" @click="dialogVisible = false">取消</el-button>
          <el-button type="primary" class="save-btn" :loading="saving" @click="handleSubmit">保存分类</el-button>
        </div>
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
.categories-page {
  padding: 24px;
  background-color: #f8fafc;
  min-height: 100%;
}

.page-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 24px;
}

.header-title {
  display: flex;
  align-items: center;
  gap: 12px;
}

.title-accent {
  width: 4px;
  height: 24px;
  background: linear-gradient(180deg, #3b82f6 0%, #2563eb 100%);
  border-radius: 4px;
}

.header-title h2 {
  margin: 0;
  font-size: 20px;
  font-weight: 600;
  color: #1e293b;
  letter-spacing: 0.5px;
}

.action-btn {
  border-radius: 8px;
  font-weight: 500;
  padding: 10px 20px;
  box-shadow: 0 4px 6px -1px rgba(59, 130, 246, 0.2);
  transition: all 0.2s ease;
}

.action-btn:hover {
  transform: translateY(-1px);
  box-shadow: 0 6px 8px -1px rgba(59, 130, 246, 0.3);
}

.panel-container {
  background: #ffffff;
  border-radius: 16px;
  padding: 20px;
  box-shadow: 0 2px 12px rgba(0, 0, 0, 0.04);
}

.tree-panel-container {
  min-height: 600px;
}

.panel-header {
  margin-bottom: 20px;
}

.panel-header h3 {
  margin: 0;
  font-size: 16px;
  font-weight: 600;
  color: #334155;
}

/* Tree Styles */
.premium-tree {
  background: transparent;
  --el-tree-node-hover-bg-color: #f1f5f9;
}

:deep(.premium-tree .el-tree-node__content) {
  height: 40px;
  border-radius: 8px;
  margin-bottom: 4px;
  transition: all 0.2s;
}

.tree-node-wrapper {
  display: flex;
  align-items: center;
  justify-content: space-between;
  width: 100%;
  padding-right: 8px;
}

.tree-node-content {
  display: flex;
  align-items: center;
  gap: 8px;
}

.node-icon {
  color: #94a3b8;
  font-size: 16px;
  display: flex;
  align-items: center;
  justify-content: center;
  width: 20px;
}

.node-icon.folder {
  font-size: 14px;
}

.node-label {
  font-weight: 500;
  color: #334155;
}

.tree-actions {
  opacity: 0;
  transition: opacity 0.2s ease;
  display: flex;
  gap: 4px;
}

:deep(.premium-tree .el-tree-node__content:hover) .tree-actions {
  opacity: 1;
}

/* Table Styles */
.premium-table {
  --el-table-border-color: transparent;
  --el-table-header-bg-color: #f8fafc;
}

:deep(.premium-header th) {
  background-color: #f8fafc !important;
  color: #64748b;
  font-weight: 600;
  text-transform: uppercase;
  font-size: 12px;
  letter-spacing: 0.5px;
  padding: 12px 0;
  border-bottom: none !important;
}

:deep(.premium-row) {
  transition: all 0.2s ease;
}

:deep(.premium-row td) {
  border-bottom: 1px solid #f1f5f9;
  padding: 16px 0;
}

:deep(.premium-row:hover > td) {
  background-color: #f8fafc !important;
}

.text-muted {
  color: #94a3b8;
}

.type-cell {
  display: flex;
  align-items: center;
  gap: 8px;
}

.status-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background-color: #cbd5e1;
}

.status-dot.cash { background-color: #10b981; }
.status-dot.stock { background-color: #3b82f6; }
.status-dot.fund { background-color: #8b5cf6; }
.status-dot.crypto { background-color: #f59e0b; }
.status-dot.loan { background-color: #ef4444; }
.status-dot.credit_card { background-color: #f43f5e; }

.currency-tag {
  border-radius: 6px;
  font-weight: 600;
  letter-spacing: 0.5px;
}

.action-buttons {
  display: flex;
  gap: 12px;
  justify-content: center;
}

/* Dialog Styles */
:deep(.premium-dialog) {
  border-radius: 16px;
  overflow: hidden;
  box-shadow: 0 20px 40px rgba(0, 0, 0, 0.1);
}

:deep(.premium-dialog .el-dialog__header) {
  margin: 0;
  padding: 24px;
  border-bottom: 1px solid #f1f5f9;
  background: #ffffff;
}

:deep(.premium-dialog .el-dialog__title) {
  font-weight: 600;
  font-size: 18px;
  color: #1e293b;
}

:deep(.premium-dialog .el-dialog__body) {
  padding: 32px 24px;
  background: #f8fafc;
}

:deep(.premium-dialog .el-dialog__footer) {
  padding: 16px 24px;
  border-top: 1px solid #f1f5f9;
  background: #ffffff;
  margin: 0;
}

.premium-form .el-form-item {
  margin-bottom: 24px;
}

.premium-form :deep(.el-input__wrapper) {
  box-shadow: 0 1px 2px rgba(0, 0, 0, 0.05);
  border-radius: 8px;
  padding: 6px 12px;
}

.dialog-footer {
  display: flex;
  justify-content: flex-end;
  gap: 12px;
}

.cancel-btn {
  border-radius: 8px;
  font-weight: 500;
}

.save-btn {
  border-radius: 8px;
  font-weight: 500;
  padding: 8px 24px;
}
</style>
