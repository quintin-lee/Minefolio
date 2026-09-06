<template>
  <el-dialog
    v-model="visible"
    title="快捷记账"
    width="520px"
    destroy-on-close
    class="quick-record-dialog"
    :before-close="handleClose"
  >
    <el-form ref="formRef" :model="form" :rules="rules" label-width="80px" class="quick-form">
      <el-form-item label="记账类型" prop="recordType">
        <el-radio-group v-model="form.recordType" size="default" class="type-radio-group">
          <el-radio-button label="expense">支出</el-radio-button>
          <el-radio-button label="income">收入</el-radio-button>
          <el-radio-button label="transfer">转账</el-radio-button>
        </el-radio-group>
      </el-form-item>

      <el-form-item label="金额" prop="amount">
        <el-input-number
          v-model="form.amount"
          :precision="2"
          :step="10"
          :min="0.01"
          placeholder="0.00"
          style="width: 100%"
        />
      </el-form-item>

      <el-form-item :label="form.recordType === 'transfer' ? '转出账户' : '扣款账户'" prop="asset_id">
        <el-select v-model="form.asset_id" placeholder="选择资产账户" style="width: 100%" filterable>
          <el-option
            v-for="a in assets"
            :key="a.id"
            :label="`${a.name} (${formatAssetBalance(a)})`"
            :value="a.id"
          />
        </el-select>
      </el-form-item>

      <el-form-item v-if="form.recordType === 'transfer'" label="转入账户" prop="target_asset_id">
        <el-select v-model="form.target_asset_id" placeholder="选择转入账户" style="width: 100%" filterable>
          <el-option
            v-for="a in assets"
            :key="a.id"
            :label="`${a.name} (${formatAssetBalance(a)})`"
            :value="a.id"
            :disabled="a.id === form.asset_id"
          />
        </el-select>
      </el-form-item>

      <el-form-item v-if="form.recordType !== 'transfer'" label="分类" prop="category_id">
        <el-cascader
          v-model="form.category_id"
          :options="categoryOptions as any"
          :props="{ checkStrictly: true, emitPath: false, value: 'id', label: 'name', children: 'children' }"
          placeholder="选择分类"
          style="width: 100%"
          clearable
        />
      </el-form-item>

      <el-form-item label="日期" prop="date">
        <el-date-picker
          v-model="form.date"
          type="date"
          placeholder="选择日期"
          value-format="YYYY-MM-DD"
          style="width: 100%"
        />
      </el-form-item>

      <el-form-item label="备注" prop="note">
        <el-input
          v-model="form.note"
          type="textarea"
          :rows="2"
          placeholder="添加备注信息（可选）"
        />
      </el-form-item>
    </el-form>

    <template #footer>
      <div class="dialog-footer">
        <el-button @click="handleClose">取消</el-button>
        <el-button type="primary" :loading="submitting" @click="handleSubmit">
          保存记录
        </el-button>
      </div>
    </template>
  </el-dialog>
</template>

<script setup lang="ts">
import { ref, reactive, computed, onMounted } from 'vue'
import { ElMessage, type FormInstance, type FormRules } from 'element-plus'
import { assetsApi } from '@/api/assets'
import { dailyExpensesApi } from '@/api/daily_expenses'
import { transactionsApi } from '@/api/transactions'
import { useCategoryStore } from '@/stores/category'
import { formatCurrency } from '@/utils/format'
import type { Asset } from '@/types'

const props = defineProps<{
  modelValue: boolean
}>()

const emit = defineEmits<{
  (e: 'update:modelValue', val: boolean): void
  (e: 'saved'): void
}>()

const visible = computed({
  get: () => props.modelValue,
  set: (val) => emit('update:modelValue', val)
})

const categoryStore = useCategoryStore()
const assets = ref<Asset[]>([])
const submitting = ref(false)
const formRef = ref<FormInstance>()

const form = reactive({
  recordType: 'expense' as 'expense' | 'income' | 'transfer',
  amount: undefined as number | undefined,
  asset_id: undefined as number | undefined,
  target_asset_id: undefined as number | undefined,
  category_id: undefined as number | undefined,
  date: new Date().toISOString().substring(0, 10),
  note: ''
})

const rules: FormRules = {
  recordType: [{ required: true, message: '请选择类型', trigger: 'change' }],
  amount: [{ required: true, message: '请输入金额', trigger: 'blur' }],
  asset_id: [{ required: true, message: '请选择关联资产账户', trigger: 'change' }],
  target_asset_id: [{ required: true, message: '请选择转入资产账户', trigger: 'change' }],
  category_id: [{ required: true, message: '请选择分类', trigger: 'change' }],
  date: [{ required: true, message: '请选择日期', trigger: 'change' }]
}

const categoryOptions = computed(() => {
  return form.recordType === 'income'
    ? categoryStore.buildTree(categoryStore.incomeCategories)
    : categoryStore.buildTree(categoryStore.expenseCategories)
})

function formatAssetBalance(a: Asset) {
  return formatCurrency(a.current_value ?? 0, a.currency)
}

async function loadAssets() {
  try {
    const res = await assetsApi.list({ page: 1, page_size: 100 })
    if (res.list) {
      assets.value = res.list
    }
  } catch (err) {
    console.error('Failed to load assets', err)
  }
}

function handleClose() {
  visible.value = false
  if (formRef.value) {
    formRef.value.resetFields()
  }
}

async function handleSubmit() {
  if (!formRef.value) return
  await formRef.value.validate(async (valid) => {
    if (!valid) return
    submitting.value = true
    try {
      if (form.recordType === 'transfer') {
        // 转账：通过 transactions API
        await transactionsApi.create({
          asset_id: form.asset_id!,
          linked_asset_id: form.target_asset_id,
          transaction_type: 'transfer_out',
          amount: form.amount!,
          transaction_date: `${form.date} 12:00:00`,
          currency: 'CNY',
          category_id: 0,
          note: form.note || '账户间转账'
        })
      } else {
        // 收支：通过 dailyExpenses API
        await dailyExpensesApi.create({
          asset_id: form.asset_id!,
          category_id: form.category_id!,
          expense_type: form.recordType,
          amount: form.amount!,
          expense_date: form.date,
          note: form.note
        })
      }
      ElMessage.success('记账成功！')
      emit('saved')
      handleClose()
      // 发送全局刷新事件
      window.dispatchEvent(new CustomEvent('minefolio:data-updated'))
    } catch (err: any) {
      ElMessage.error(err.message || '记账失败，请检查输入')
    } finally {
      submitting.value = false
    }
  })
}

onMounted(() => {
  loadAssets()
  categoryStore.loadCategories()
})
</script>

<style scoped>
.quick-form {
  padding: 8px 0;
}
.type-radio-group {
  width: 100%;
  display: flex;
}
.type-radio-group :deep(.el-radio-button) {
  flex: 1;
}
.type-radio-group :deep(.el-radio-button__inner) {
  width: 100%;
}
.dialog-footer {
  display: flex;
  justify-content: flex-end;
  gap: 12px;
}
</style>
