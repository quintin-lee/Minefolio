<template>
  <el-drawer v-model="visible" direction="btt" size="75%" :with-header="false" class="quick-sheet">
    <div class="sheet-body">
      <div class="sheet-header-actions">
        <div class="type-switch">
          <el-radio-group v-model="form.expense_type">
            <el-radio-button value="expense">支出</el-radio-button>
            <el-radio-button value="income">收入</el-radio-button>
          </el-radio-group>
        </div>
        <el-button size="small" type="primary" plain :loading="scanning" class="ocr-btn" @click="triggerFilePick">
          <Icon icon="ph:camera-bold" class="ocr-icon" /> 识单
        </el-button>
        <input ref="fileInputRef" type="file" accept="image/*" capture="environment" style="display: none" @change="onFileSelected" />
      </div>

      <div class="amount-input">
        <span class="currency">{{ selectedCurrencySymbol }}</span>
        <input
          ref="amountRef"
          v-model="form.amount"
          type="text"
          inputmode="decimal"
          placeholder="0.00"
          class="amount-field"
        />
      </div>

      <el-form label-position="top">
        <el-form-item label="分类">
          <el-cascader
            v-model="form.category_id"
            :options="categoryTree as any"
            :props="{ checkStrictly: true, emitPath: false, value: 'id', label: 'name', children: 'children' }"
            placeholder="选择分类"
            style="width: 100%"
            clearable
            filterable
          />
        </el-form-item>
        <el-form-item label="资产">
          <el-select v-model="form.asset_id" filterable placeholder="选择资产" style="width:100%">
            <el-option v-for="a in assets" :key="a.id" :label="assetLabel(a)" :value="Number(a.id)" />
          </el-select>
        </el-form-item>
        <el-form-item label="日期">
          <el-date-picker v-model="form.expense_date" type="date" value-format="YYYY-MM-DD" :editable="false" style="width:100%" />
        </el-form-item>
        <el-form-item label="备注">
          <el-input v-model="form.note" placeholder="可选" />
        </el-form-item>
        <el-form-item label="标签">
          <el-select v-model="form.tags" multiple filterable placeholder="可选" style="width:100%">
            <el-option v-for="tag in localTags" :key="tag.id" :label="tag.name" :value="Number(tag.id)" />
          </el-select>
        </el-form-item>
      </el-form>

      <el-button type="primary" size="large" :loading="saving" class="save-btn" @click="save">保存</el-button>
    </div>
  </el-drawer>
</template>

<script setup lang="ts">
import { ref, reactive, computed, onMounted, onBeforeUnmount, nextTick, watch } from 'vue'
import { ElMessage } from 'element-plus'
import { Icon } from '@iconify/vue'
import { useCategoryStore } from '@/stores/category'
import { useLedgerStore } from '@/stores/ledger'
import { assetsApi } from '@/api/assets'
import { offlineApi } from '@/utils/offline-http'
import { receiptsApi } from '@/api/receipts'
import { query, rowsFrom } from '@/db/local'
import { localToday, assetLabel } from '@/utils/format'
import { prepareCategoryTree, isIdInTree, findCategoryByName } from '@/utils/category-tree'
import type { DailyExpense, Category, Asset, Tag } from '@/types'

const props = defineProps<{ modelValue?: boolean; record?: DailyExpense | null }>()
const emit = defineEmits<{ (e: 'saved'): void; (e: 'update:modelValue', v: boolean): void }>()

const visible = computed({
  get: () => props.modelValue ?? false,
  set: (v: boolean) => emit('update:modelValue', v),
})
const amountRef = ref<HTMLInputElement | null>(null)
const fileInputRef = ref<HTMLInputElement | null>(null)
const saving = ref(false)
const scanning = ref(false)
const offlineCategories = ref<Category[]>([])
const assets = ref<Asset[]>([])
const categoryStore = useCategoryStore()
const ledgerStore = useLedgerStore()

const form = reactive({
  expense_type: 'expense' as 'income' | 'expense',
  category_id: null as number | null,
  asset_id: null as number | null,
  amount: '' as string | number,
  expense_date: localToday(),
  note: '',
  tags: [] as number[],
})
const localTags = ref<Tag[]>([])

const rawCategories = computed<Category[]>(() => {
  if (categoryStore.incomeExpenseCategories.length > 0) {
    return categoryStore.incomeExpenseCategories
  }
  return offlineCategories.value
})

const categoryTree = computed(() => {
  return prepareCategoryTree(rawCategories.value, form.expense_type)
})

const CURRENCY_SYMBOLS: Record<string, string> = {
  CNY: '¥',
  JPY: '¥',
  USD: '$',
  EUR: '€',
  GBP: '£',
  HKD: 'HK$',
}

const selectedCurrencySymbol = computed(() => {
  const cur = assets.value.find(a => Number(a.id) === Number(form.asset_id))?.currency || 'CNY'
  return CURRENCY_SYMBOLS[cur] || `${cur} `
})

watch(
  () => form.expense_type,
  (newType, oldType) => {
    if (oldType !== undefined && newType !== oldType) {
      if (form.category_id && !isIdInTree(categoryTree.value, form.category_id)) {
        form.category_id = null
      }
    }
  }
)

function triggerFilePick() {
  fileInputRef.value?.click()
}

let activeScanReader: FileReader | null = null

onBeforeUnmount(() => {
  activeScanReader?.abort()
  activeScanReader = null
  scanning.value = false
})

async function onFileSelected(e: Event) {
  const file = (e.target as HTMLInputElement).files?.[0]
  if (!file) return
  if (!file.type.startsWith('image/')) {
    ElMessage.warning('请选择票据图片文件')
    if (fileInputRef.value) fileInputRef.value.value = ''
    return
  }

  activeScanReader?.abort()
  activeScanReader = null
  scanning.value = true
  const reader = new FileReader()
  activeScanReader = reader
  reader.onload = async () => {
    if (activeScanReader !== reader) return
    activeScanReader = null
    const base64 = reader.result as string
    try {
      const res = await receiptsApi.scan({ image: base64 })
      if (res) {
        if (res.amount > 0) form.amount = res.amount
        if (res.date) form.expense_date = res.date
        if (res.type) form.expense_type = res.type
        if (res.category_id && isIdInTree(categoryTree.value, Number(res.category_id))) {
          form.category_id = Number(res.category_id)
        } else if (res.category_name) {
          const matched = findCategoryByName(categoryTree.value, res.category_name)
          if (matched) form.category_id = Number(matched.id)
        }
        const noteParts = [res.counterparty, res.description].filter(Boolean)
        if (noteParts.length > 0) {
          form.note = noteParts.join(' - ')
        }
        ElMessage.success(`票据识别成功！金额 ¥${res.amount || 0}`)
      }
    } catch (err: any) {
      ElMessage.error(err?.message || '票据识别失败，请手动录入')
    } finally {
      scanning.value = false
      if (fileInputRef.value) fileInputRef.value.value = ''
    }
  }
  reader.onerror = () => {
    if (activeScanReader !== reader) return
    activeScanReader = null
    scanning.value = false
    if (fileInputRef.value) fileInputRef.value.value = ''
    ElMessage.error('读取图片失败')
  }
  reader.readAsDataURL(file)
}

function populateForm() {
  if (props.record) {
    form.expense_type = props.record.expense_type
    form.category_id = Number(props.record.category_id)
    form.asset_id = Number(props.record.asset_id)
    form.amount = props.record.amount
    form.expense_date = props.record.expense_date
    form.note = props.record.note ?? ''
    form.tags = (props.record.tags ?? []).map((tag) => Number(tag.id))
  } else {
    form.expense_type = 'expense'
    form.category_id = null
    form.asset_id = null
    form.amount = ''
    form.expense_date = localToday()
    form.note = ''
    form.tags = []
  }
}

watch(
  () => props.modelValue,
  (val) => {
    if (val) {
      populateForm()
      nextTick(() => {
        amountRef.value?.focus()
      })
    }
  },
  { immediate: true }
)

watch(
  () => props.record,
  () => {
    if (props.modelValue) {
      populateForm()
    }
  }
)

onMounted(async () => {
  try {
    await categoryStore.loadCategories()
  } catch {
    try {
      const res = query('SELECT * FROM categories WHERE __deleted = 0')
      const rows = rowsFrom(res) as unknown as Category[]
      offlineCategories.value = rows.filter((c) => c.type === 'income' || c.type === 'expense')
    } catch (e) {
      console.error('[ExpenseQuickSheet] load local categories failed:', e)
    }
  }

  try {
    const res = query('SELECT * FROM tags WHERE __deleted = 0 ORDER BY id')
    localTags.value = rowsFrom(res) as unknown as Tag[]
  } catch (e) {
    console.error('[ExpenseQuickSheet] load local tags failed:', e)
  }

  try {
    const res = await assetsApi.list({ page_size: 500 })
    assets.value = res.list
  } catch {
    try {
      const currentLedgerId = ledgerStore.currentLedgerId || 0
      const res = query('SELECT * FROM assets WHERE __deleted = 0 AND (ledger_id = ? OR ledger_id = 0)', [currentLedgerId])
      assets.value = rowsFrom(res) as unknown as Asset[]
    } catch (e) {
      console.error('[ExpenseQuickSheet] load local assets failed:', e)
    }
  }

  populateForm()
  if (props.modelValue) {
    await nextTick()
    amountRef.value?.focus()
  }
})

async function save() {
  if (saving.value) return
  const amt = Number(form.amount)
  if (!Number.isFinite(amt) || amt <= 0) return ElMessage.warning('请输入有效金额')
  if (!form.category_id || !form.asset_id) return ElMessage.warning('请选择分类和资产')
  saving.value = true
  const selectedAsset = assets.value.find(a => Number(a.id) === Number(form.asset_id))
  const payload: Record<string, unknown> = {
    expense_type: form.expense_type,
    category_id: form.category_id,
    asset_id: form.asset_id,
    amount: amt,
    currency: selectedAsset?.currency || 'CNY',
    expense_date: form.expense_date,
    note: form.note,
    ledger_id: ledgerStore.currentLedgerId || 0,
  }
  if (form.tags.length > 0) payload.tags = form.tags
  try {
    if (props.record) await offlineApi.put(`/daily-expenses/${props.record.id}`, payload)
    else await offlineApi.post('/daily-expenses', payload)
    ElMessage.success(`已记录 ${payload.currency} ${amt}`)
    visible.value = false
    emit('saved')
  } catch (err: any) {
    ElMessage.error(err?.response?.data?.message || err?.message || '保存失败')
  } finally {
    saving.value = false
  }
}
</script>

<style scoped>
:deep(.quick-sheet) {
  max-width: 100vw;
  box-sizing: border-box;
}

:deep(.quick-sheet .el-drawer__body) {
  padding: 0 !important;
  box-sizing: border-box;
  overflow-x: hidden;
  max-width: 100vw;
}

.sheet-body {
  box-sizing: border-box;
  width: 100%;
  height: 100%;
  max-width: 100vw;
  overflow-x: hidden;
  overflow-y: auto;
  -webkit-overflow-scrolling: touch;
  padding: 16px;
  padding-bottom: calc(16px + env(safe-area-inset-bottom));
}

.sheet-header-actions {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 16px;
  width: 100%;
  box-sizing: border-box;
}

.type-switch {
  display: flex;
}

.ocr-btn {
  display: flex;
  align-items: center;
  gap: 4px;
  border-radius: 16px;
  font-weight: 600;
}

.ocr-icon {
  font-size: 15px;
}

.amount-input {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 20px;
  width: 100%;
  box-sizing: border-box;
}

.currency {
  font-size: 24px;
  color: var(--mf-text-muted);
  flex-shrink: 0;
}

.amount-field {
  flex: 1;
  min-width: 0;
  width: 100%;
  box-sizing: border-box;
  background: transparent;
  border: none;
  border-bottom: 2px solid var(--mf-border);
  color: var(--mf-text-main);
  font-size: 32px;
  font-family: 'JetBrains Mono', monospace;
  outline: none;
  text-align: right;
}

.amount-field:focus {
  border-color: var(--mf-primary);
}

:deep(.el-form-item) {
  margin-bottom: 16px;
  width: 100%;
  box-sizing: border-box;
}

:deep(.el-form-item__content) {
  width: 100%;
  min-width: 0;
  box-sizing: border-box;
}

:deep(.el-cascader),
:deep(.el-select),
:deep(.el-date-editor) {
  width: 100% !important;
  max-width: 100% !important;
  box-sizing: border-box;
}

.save-btn {
  width: 100%;
  margin-top: 16px;
  height: 44px;
  font-size: 16px;
  border-radius: var(--mf-radius-md);
  box-sizing: border-box;
}
</style>
