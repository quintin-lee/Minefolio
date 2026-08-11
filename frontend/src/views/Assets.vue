<template>
  <div class="assets-page">
    <el-card>
      <template #header>
        <div class="card-header">
          <span>资产管理</span>
          <el-button type="primary" @click="openDialog()">
            <el-icon><Plus /></el-icon> 新增资产
          </el-button>
        </div>
      </template>

      <!-- 总资产概览 -->
      <el-row :gutter="12" class="asset-summary">
        <el-col :span="8">
          <el-statistic title="总资产" :value="totalAssets" :precision="2" prefix="¥" />
        </el-col>
        <el-col :span="8">
          <el-statistic title="总负债" :value="totalLiabilities" :precision="2" prefix="¥" />
        </el-col>
        <el-col :span="8">
          <el-statistic title="净资产" :value="netWorth" :precision="2" prefix="¥" />
        </el-col>
      </el-row>

      <!-- 资产列表 -->
      <el-table :data="assets" stripe style="margin-top: 16px">
        <el-table-column prop="name" label="名称" />
        <el-table-column prop="category_name" label="分类" />
        <el-table-column prop="account_no" label="账户编号" />
        <el-table-column prop="currency" label="币种" width="80" />
        <el-table-column prop="current_value" label="当前价值" width="140">
          <template #default="{ row }">{{ formatCurrency(row.current_value) }}</template>
        </el-table-column>
        <el-table-column label="操作" width="120">
          <template #default="{ row }">
            <el-button link type="primary" @click="openDialog(row)">编辑</el-button>
            <el-button link type="danger" @click="handleDelete(row)">删除</el-button>
          </template>
        </el-table-column>
      </el-table>
    </el-card>

    <!-- 新增/编辑对话框 -->
    <el-dialog v-model="dialogVisible" :title="editingId ? '编辑资产' : '新增资产'" width="480px">
      <el-form ref="formRef" :model="form" :rules="rules" label-width="100px">
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
          <el-input-number v-model="form.current_value" :precision="2" :min="0" style="width: 100%" />
        </el-form-item>
        <el-form-item label="币种">
          <el-select v-model="form.currency" style="width: 100%">
            <el-option label="CNY" value="CNY" />
            <el-option label="USD" value="USD" />
            <el-option label="EUR" value="EUR" />
          </el-select>
        </el-form-item>
        <el-form-item label="备注">
          <el-input v-model="form.note" type="textarea" :rows="2" />
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
.assets-page { }
.card-header { display: flex; justify-content: space-between; align-items: center; }
.asset-summary { margin-bottom: 8px; }
</style>
