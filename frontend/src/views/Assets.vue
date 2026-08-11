<template>
  <div class="assets-page">
    <div class="page-header">
      <div class="header-title">
        <div class="title-accent"></div>
        <h2>资产管理</h2>
      </div>
      <el-button type="primary" class="action-btn" @click="openDialog()">
        <el-icon><Plus /></el-icon> 新增资产
      </el-button>
    </div>

    <!-- 总资产概览 -->
    <el-row :gutter="24" class="asset-summary">
      <el-col :span="8">
        <div class="summary-card">
          <div class="summary-label">总资产</div>
          <div class="summary-value income-text">{{ formatCurrency(totalAssets) }}</div>
        </div>
      </el-col>
      <el-col :span="8">
        <div class="summary-card">
          <div class="summary-label">总负债</div>
          <div class="summary-value expense-text">{{ formatCurrency(totalLiabilities) }}</div>
        </div>
      </el-col>
      <el-col :span="8">
        <div class="summary-card highlight-card">
          <div class="summary-label">净资产</div>
          <div class="summary-value">{{ formatCurrency(netWorth) }}</div>
        </div>
      </el-col>
    </el-row>

    <!-- 资产列表 -->
    <div class="table-container">
      <el-table :data="assets" class="premium-table" row-class-name="premium-row" header-cell-class-name="premium-header">
        <el-table-column label="名称" min-width="150">
          <template #default="{ row }">
            <div class="asset-name-cell">
              <span class="asset-icon">{{ {'bank': '🏦', 'cash': '💵', 'alipay': '📱', 'wechat': '💬', 'credit_card': '💳', 'stock': '📈', 'fund': '📊', 'crypto': '🪙', 'loan': '💸', 'real_estate': '🏠'}[row.asset_type as string] || '💼' }}</span>
              <span class="asset-name">{{ row.name }}</span>
            </div>
          </template>
        </el-table-column>
        <el-table-column prop="category_name" label="分类" min-width="120" />
        <el-table-column prop="account_no" label="账户编号" min-width="140" />
        <el-table-column label="币种" width="100">
          <template #default="{ row }">
            <el-tag size="small" class="currency-tag" effect="plain">{{ row.currency }}</el-tag>
          </template>
        </el-table-column>
        <el-table-column label="当前价值" min-width="160" align="right">
          <template #default="{ row }">
            <span class="mono-amount">{{ formatCurrency(row.current_value) }}</span>
          </template>
        </el-table-column>
        <el-table-column label="操作" width="140" align="center">
          <template #default="{ row }">
            <div class="action-buttons">
              <el-button link type="primary" @click="openDialog(row)">编辑</el-button>
              <el-button link type="danger" @click="handleDelete(row)">删除</el-button>
            </div>
          </template>
        </el-table-column>
      </el-table>
    </div>

    <!-- 新增/编辑对话框 -->
    <el-dialog v-model="dialogVisible" :title="editingId ? '编辑资产' : '新增资产'" width="480px" class="premium-dialog" :show-close="false">
      <el-form ref="formRef" :model="form" :rules="rules" label-width="90px" class="premium-form">
        <el-form-item label="资产名称" prop="name">
          <el-input v-model="form.name" placeholder="如：招商银行卡、茅台股票" />
        </el-form-item>
        <el-form-item label="分类" prop="category_id">
          <el-cascader v-model="form._catPath" :options="categoryTree" :props="{ checkStrictly: true, value: 'id', label: 'name' }"
            placeholder="选择分类" style="width: 100%" @change="onCatChange" />
        </el-form-item>
        <el-form-item label="账户编号">
          <el-input v-model="form.account_no" placeholder="可选" />
        </el-form-item>
        <el-form-item label="当前价值" prop="current_value">
          <el-input-number v-model="form.current_value" :precision="2" :min="0" style="width: 100%" :controls="false" />
        </el-form-item>
        <el-form-item label="币种">
          <el-select v-model="form.currency" style="width: 100%">
            <el-option label="CNY" value="CNY" />
            <el-option label="USD" value="USD" />
            <el-option label="EUR" value="EUR" />
          </el-select>
        </el-form-item>
        <el-form-item label="备注">
          <el-input v-model="form.note" type="textarea" :rows="3" placeholder="添加备注..." />
        </el-form-item>
      </el-form>
      <template #footer>
        <div class="dialog-footer">
          <el-button class="cancel-btn" @click="dialogVisible = false">取消</el-button>
          <el-button class="save-btn" type="primary" :loading="saving" @click="handleSubmit">保存资产</el-button>
        </div>
      </template>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, onMounted, computed } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { assetsApi } from '@/api/assets'
import { categoriesApi } from '@/api/categories'
import type { Asset, Category } from '@/types'

const assets = ref<Asset[]>([])
const categoryTree = ref<Category[]>([])
const dialogVisible = ref(false)
const editingId = ref<number | null>(null)
const saving = ref(false)
const formRef = ref()

const totalAssets = computed(() => assets.value.filter(a => !isLiability(a)).reduce((s, a) => s + a.current_value, 0))
const totalLiabilities = computed(() => assets.value.filter(isLiability).reduce((s, a) => s + a.current_value, 0))
const netWorth = computed(() => totalAssets.value - totalLiabilities.value)

function isLiability(asset: Asset) {
  const types = ['loan', 'credit_card', 'other_liability']
  return types.includes(asset.asset_type || '')
}

function formatCurrency(val: number) {
  return new Intl.NumberFormat('zh-CN', { style: 'currency', currency: 'CNY' }).format(val)
}

async function loadAssets() {
  const res = await assetsApi.list()
  assets.value = res
}

async function loadCategories() {
  const res = await categoriesApi.list()
  categoryTree.value = res
}

function openDialog(asset?: any) {
  editingId.value = asset?.id ?? null
  Object.assign(form, asset ? {
    name: asset.name, category_id: asset.category_id, account_no: asset.account_no,
    current_value: asset.current_value, currency: asset.currency, note: asset.note,
    _catPath: [asset.category_id],
  } : { name: '', category_id: null, account_no: '', current_value: 0, currency: 'CNY', note: '', _catPath: [] })
  dialogVisible.value = true
}

const form = reactive({ name: '', category_id: null as number | null, account_no: '', current_value: 0, currency: 'CNY', note: '', _catPath: [] as number[] })
const rules = { name: [{ required: true, message: '请输入资产名称' }], category_id: [{ required: true, message: '请选择分类' }] }

function onCatChange(val: any) {
  const arr = (val as number[]) ?? []
  form.category_id = arr[arr.length - 1] ?? null
}

async function handleSubmit() {
  await formRef.value.validate(async (valid: boolean) => {
    if (!valid) return
    saving.value = true
    try {
      const data = { name: form.name, category_id: form.category_id, account_no: form.account_no, current_value: form.current_value, currency: form.currency, note: form.note }
      if (editingId.value) {
        await assetsApi.update(editingId.value, data)
        ElMessage.success('更新成功')
      } else {
        await assetsApi.create(data)
        ElMessage.success('创建成功')
      }
      dialogVisible.value = false
      loadAssets()
    } finally { saving.value = false }
  })
}

async function handleDelete(asset: any) {
  await ElMessageBox.confirm(`确定删除资产「${asset.name}」吗？`, '提示', { type: 'warning' })
  await assetsApi.delete(asset.id)
  ElMessage.success('删除成功')
  loadAssets()
}

onMounted(() => { loadAssets(); loadCategories() })
</script>

<style scoped>
.assets-page {
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

.asset-summary {
  margin-bottom: 24px;
}

.summary-card {
  background: #ffffff;
  border-radius: 16px;
  padding: 24px;
  box-shadow: 0 2px 12px rgba(0, 0, 0, 0.04);
  display: flex;
  flex-direction: column;
  gap: 8px;
  transition: transform 0.2s ease;
}

.summary-card:hover {
  transform: translateY(-2px);
  box-shadow: 0 8px 24px rgba(0, 0, 0, 0.06);
}

.highlight-card {
  background: linear-gradient(135deg, #1e293b 0%, #0f172a 100%);
}

.highlight-card .summary-label {
  color: #94a3b8;
}

.highlight-card .summary-value {
  color: #ffffff;
}

.summary-label {
  font-size: 14px;
  color: #64748b;
  font-weight: 500;
}

.summary-value {
  font-size: 28px;
  font-weight: 700;
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
  letter-spacing: -0.5px;
}

.income-text {
  color: #10b981;
}

.expense-text {
  color: #ef4444;
}

.table-container {
  background: #ffffff;
  border-radius: 16px;
  padding: 16px;
  box-shadow: 0 2px 12px rgba(0, 0, 0, 0.04);
}

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

.asset-name-cell {
  display: flex;
  align-items: center;
  gap: 12px;
}

.asset-icon {
  font-size: 20px;
  background: #f1f5f9;
  padding: 8px;
  border-radius: 10px;
  display: flex;
  align-items: center;
  justify-content: center;
  line-height: 1;
}

.asset-name {
  font-weight: 500;
  color: #334155;
}

.currency-tag {
  border-radius: 6px;
  font-weight: 600;
  letter-spacing: 0.5px;
}

.mono-amount {
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
  font-weight: 600;
  color: #1e293b;
  font-size: 15px;
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
