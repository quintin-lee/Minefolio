<script setup lang="ts">
import { ref, reactive, watch } from 'vue'
import { ElMessage, type FormInstance, type FormRules } from 'element-plus'
import { Icon } from '@iconify/vue'
import { accountsApi } from '@/api/accounts'
import type { Account } from '@/types'

const props = defineProps<{
  visible: boolean
  account?: Account | null
}>()

const emit = defineEmits<{
  (e: 'update:visible', val: boolean): void
  (e: 'saved'): void
}>()

const formRef = ref<FormInstance>()
const submitting = ref(false)
const archiving = ref(false)

const institutionOptions = [
  { value: 'bank_card', label: '银行卡', icon: 'ph:credit-card' },
  { value: 'platform', label: '平台账户', icon: 'ph:phone' },
  { value: 'broker', label: '券商', icon: 'ph:chart-line-up' },
  { value: 'fund_platform', label: '基金平台', icon: 'ph:bank' },
  { value: 'cash_wallet', label: '现金钱包', icon: 'ph:wallet' },
  { value: 'other', label: '其他', icon: 'ph:briefcase' },
]

const iconOptions = [
  'ph:wallet',
  'ph:credit-card',
  'ph:bank',
  'ph:phone',
  'ph:chart-line-up',
  'ph:briefcase',
  'ph:piggy-bank',
  'ph:device-mobile',
  'ph:currency-circle-dollar',
  'ph:house',
]

const form = reactive({
  name: '',
  institution: 'bank_card',
  account_no: '',
  currency: 'CNY',
  icon: 'ph:wallet',
  note: '',
  sort_order: 0,
})

const rules: FormRules = {
  name: [{ required: true, message: '请输入账户名称', trigger: 'blur' }],
}

watch(
  () => props.visible,
  (val) => {
    if (!val) return
    if (props.account) {
      form.name = props.account.name
      form.institution = props.account.institution
      form.account_no = props.account.account_no ?? ''
      form.currency = props.account.currency || 'CNY'
      form.icon = props.account.icon || 'ph:wallet'
      form.note = props.account.note ?? ''
      form.sort_order = props.account.sort_order || 0
    } else {
      form.name = ''
      form.institution = 'bank_card'
      form.account_no = ''
      form.currency = 'CNY'
      form.icon = 'ph:wallet'
      form.note = ''
      form.sort_order = 0
    }
  }
)

async function handleSubmit() {
  if (!formRef.value) return
  await formRef.value.validate()
  submitting.value = true
  try {
    if (props.account?.id) {
      await accountsApi.update(props.account.id, form)
      ElMessage.success('账户更新成功')
    } else {
      await accountsApi.create(form)
      ElMessage.success('账户创建成功')
    }
    emit('saved')
    emit('update:visible', false)
  } catch (err: any) {
    ElMessage.error(err?.message || '保存失败')
  } finally {
    submitting.value = false
  }
}

async function handleToggleArchive() {
  const acc = props.account
  if (!acc) return
  archiving.value = true
  try {
    await accountsApi.update(acc.id, { archived: !acc.archived })
    emit('saved')
  } catch (err: any) {
    ElMessage.error(err?.message || '操作失败')
  } finally {
    archiving.value = false
  }
}

function handleDelete() {
  const acc = props.account
  if (!acc) return
  accountsApi
    .delete(acc.id)
    .then(() => {
      ElMessage.success('账户删除成功')
      emit('saved')
      emit('update:visible', false)
    })
    .catch((err: any) => ElMessage.error(err?.message || '删除失败'))
}

function institutionLabel(value: string) {
  return institutionOptions.find(o => o.value === value)?.label || value
}
</script>

<template>
  <el-dialog
    :model-value="visible"
    :title="account ? '编辑账户' : '新建账户'"
    width="480px"
    destroy-on-close
    append-to-body
    class="premium-dialog"
    @update:model-value="emit('update:visible', $event)"
  >
    <el-form ref="formRef" :model="form" :rules="rules" label-width="70px" class="account-form">
      <el-form-item label="名称" prop="name">
        <el-input v-model="form.name" placeholder="如：招商银行储蓄卡、支付宝" />
      </el-form-item>

      <el-form-item label="机构类型" prop="institution">
        <el-select v-model="form.institution" style="width: 100%">
          <el-option v-for="o in institutionOptions" :key="o.value" :label="o.label" :value="o.value" />
        </el-select>
      </el-form-item>

      <el-form-item label="账户编号">
        <el-input v-model="form.account_no" placeholder="可选，尾号或脱敏卡号" />
      </el-form-item>

      <el-form-item label="币种">
        <el-select v-model="form.currency" style="width: 100%">
          <el-option label="CNY" value="CNY" />
          <el-option label="USD" value="USD" />
          <el-option label="HKD" value="HKD" />
          <el-option label="EUR" value="EUR" />
        </el-select>
      </el-form-item>

      <el-form-item label="图标">
        <div class="icon-grid">
          <div
            v-for="ic in iconOptions"
            :key="ic"
            class="icon-cell"
            :class="{ active: form.icon === ic }"
            @click="form.icon = ic"
          >
            <Icon :icon="ic" />
          </div>
        </div>
      </el-form-item>

      <el-form-item label="排序">
        <el-input-number v-model="form.sort_order" :min="0" :max="999" style="width: 100%" />
      </el-form-item>

      <el-form-item label="备注">
        <el-input v-model="form.note" type="textarea" :rows="2" placeholder="添加备注..." />
      </el-form-item>
    </el-form>

    <template #footer>
      <div class="dialog-footer">
        <template v-if="account?.id">
          <el-button v-if="!account.asset_count" type="danger" plain @click="handleDelete">删除</el-button>
          <el-tooltip v-else content="该账户下仍有资产，请先移除资产" placement="top">
            <el-button type="danger" plain disabled>删除</el-button>
          </el-tooltip>
          <el-button :loading="archiving" @click="handleToggleArchive">
            {{ account.archived ? '恢复' : '归档' }}
          </el-button>
        </template>
        <div class="footer-right">
          <el-button @click="emit('update:visible', false)">取消</el-button>
          <el-button type="primary" :loading="submitting" @click="handleSubmit">保存</el-button>
        </div>
      </div>
    </template>

    <div class="institution-hint">
      机构类型：{{ institutionLabel(account?.institution || form.institution) }}
    </div>
  </el-dialog>
</template>

<style scoped>
.account-form {
  margin-top: 8px;
}
.icon-grid {
  display: grid;
  grid-template-columns: repeat(5, 1fr);
  gap: 8px;
  width: 100%;
}
.icon-cell {
  display: flex;
  align-items: center;
  justify-content: center;
  height: 40px;
  border: 1px solid var(--el-border-color);
  border-radius: 8px;
  cursor: pointer;
  font-size: 18px;
  color: var(--el-text-color-regular);
  transition: all 0.15s;
}
.icon-cell:hover {
  border-color: var(--el-color-primary);
  color: var(--el-color-primary);
}
.icon-cell.active {
  border-color: var(--el-color-primary);
  background: var(--el-color-primary-light-9);
  color: var(--el-color-primary);
}
.dialog-footer {
  display: flex;
  justify-content: space-between;
  width: 100%;
}
.footer-right {
  display: flex;
  gap: 8px;
}
.institution-hint {
  margin-top: 10px;
  font-size: 12px;
  color: var(--el-text-color-secondary);
}
</style>
