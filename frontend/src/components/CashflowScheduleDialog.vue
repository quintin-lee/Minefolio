<template>
  <el-dialog
    v-model="visible"
    :title="editingId ? '编辑现金流计划' : '新增被动现金流计划'"
    width="540px"
    class="premium-dialog"
    destroy-on-close
    :show-close="false"
  >
    <el-form ref="formRef" :model="form" :rules="rules" label-width="100px" class="premium-form">
      <el-form-item label="项目名称" prop="name">
        <el-input v-model="form.name" placeholder="如：某某债半年度付息、XX高股息ETF季度分红、房屋收租" />
      </el-form-item>

      <el-form-item label="收益类型" prop="flow_type">
        <el-select v-model="form.flow_type" style="width: 100%">
          <el-option label="股票/基金分红 (Dividend)" value="dividend" />
          <el-option label="债券/存款利息 (Interest)" value="interest" />
          <el-option label="不动产/租金收益 (Rent)" value="rent" />
          <el-option label="理财/本金到期 (Maturity)" value="maturity" />
          <el-option label="其他被动收入 (Other)" value="other" />
        </el-select>
      </el-form-item>

      <el-form-item label="源资产" prop="source_asset_id">
        <el-select
          v-model="form.source_asset_id"
          placeholder="选择产生收益的标的资产"
          style="width: 100%"
          filterable
        >
          <el-option
            v-for="item in assets"
            :key="item.id"
            :label="`${item.name} (${item.category_name || item.currency})`"
            :value="item.id"
          />
        </el-select>
      </el-form-item>

      <el-form-item label="收款账户" prop="target_asset_id">
        <el-select
          v-model="form.target_asset_id"
          placeholder="选择接收资金的钱包/银行卡账户"
          style="width: 100%"
          filterable
        >
          <el-option
            v-for="item in fundingAssets"
            :key="item.id"
            :label="`${item.name} (余额: ¥${Number(item.current_value).toFixed(2)})`"
            :value="item.id"
          />
        </el-select>
      </el-form-item>

      <el-row :gutter="12">
        <el-col :span="12">
          <el-form-item label="支付频次" prop="frequency">
            <el-select v-model="form.frequency" style="width: 100%">
              <el-option label="单次 (Once)" value="once" />
              <el-option label="每月 (Monthly)" value="monthly" />
              <el-option label="每季 (Quarterly)" value="quarterly" />
              <el-option label="每半年 (Semi-annual)" value="semi_annual" />
              <el-option label="每年 (Annual)" value="annual" />
            </el-select>
          </el-form-item>
        </el-col>
        <el-col :span="12">
          <el-form-item label="首次/到期日" prop="start_date">
            <el-date-picker
              v-model="form.start_date"
              type="date"
              value-format="YYYY-MM-DD"
              placeholder="选择日期"
              style="width: 100%"
            />
          </el-form-item>
        </el-col>
      </el-row>

      <el-form-item label="预计金额" prop="expected_amount">
        <el-input-number
          v-model="form.expected_amount"
          :precision="2"
          :min="0.01"
          :step="100"
          style="width: 100%"
          :controls="false"
          placeholder="每期预计到账金额 (元)"
        />
      </el-form-item>

      <el-form-item label="结束日期">
        <el-date-picker
          v-model="form.end_date"
          type="date"
          value-format="YYYY-MM-DD"
          placeholder="可选，留空代表长期持续"
          style="width: 100%"
        />
      </el-form-item>

      <el-form-item label="备注">
        <el-input v-model="form.note" type="textarea" :rows="2" placeholder="付息规则或备忘..." />
      </el-form-item>
    </el-form>

    <template #footer>
      <div class="dialog-footer">
        <el-button class="cancel-btn" @click="visible = false">取消</el-button>
        <el-button class="save-btn" type="primary" :loading="submitting" @click="handleSubmit">
          保存计划
        </el-button>
      </div>
    </template>
  </el-dialog>
</template>

<script setup lang="ts">
import { ref, reactive, computed } from 'vue'
import { ElMessage } from 'element-plus'
import type { FormInstance, FormRules } from 'element-plus'
import { cashflowApi } from '@/api/cashflow'
import type { Asset, CashflowSchedule } from '@/types'

const props = defineProps<{
  assets: Asset[]
}>()

const emit = defineEmits<{
  (e: 'success'): void
}>()

const visible = ref(false)
const editingId = ref<number | null>(null)
const submitting = ref(false)
const formRef = ref<FormInstance>()

const form = reactive({
  name: '',
  flow_type: 'dividend',
  source_asset_id: undefined as number | undefined,
  target_asset_id: undefined as number | undefined,
  frequency: 'monthly' as 'once' | 'monthly' | 'quarterly' | 'semi_annual' | 'annual',
  start_date: '',
  end_date: '',
  expected_amount: 1000,
  note: ''
})

const rules: FormRules = {
  name: [{ required: true, message: '请输入计划名称', trigger: 'blur' }],
  flow_type: [{ required: true, message: '请选择收益类型', trigger: 'change' }],
  source_asset_id: [{ required: true, message: '请选择产生收益的资产', trigger: 'change' }],
  target_asset_id: [{ required: true, message: '请选择收款账户', trigger: 'change' }],
  start_date: [{ required: true, message: '请指定首次/到期日期', trigger: 'change' }],
  expected_amount: [{ required: true, message: '请输入预计金额', trigger: 'blur' }]
}

const fundingAssets = computed(() =>
  props.assets.filter(a => ['cash', 'bank', 'alipay', 'wechat'].includes(a.asset_type || ''))
)

function open(sch?: CashflowSchedule) {
  editingId.value = sch?.id ?? null
  if (sch) {
    form.name = sch.name
    form.flow_type = sch.flow_type || 'dividend'
    form.source_asset_id = sch.source_asset_id
    form.target_asset_id = sch.target_asset_id
    form.frequency = (sch.frequency as any) || 'monthly'
    form.start_date = sch.start_date
    form.end_date = sch.end_date || ''
    form.expected_amount = sch.expected_amount
    form.note = sch.note || ''
  } else {
    const today = new Date().toISOString().slice(0, 10)
    form.name = ''
    form.flow_type = 'dividend'
    form.source_asset_id = undefined
    form.target_asset_id = fundingAssets.value[0]?.id
    form.frequency = 'monthly'
    form.start_date = today
    form.end_date = ''
    form.expected_amount = 500
    form.note = ''
  }
  visible.value = true
}

async function handleSubmit() {
  if (!formRef.value) return
  await formRef.value.validate(async valid => {
    if (!valid) return
    submitting.value = true
    try {
      if (editingId.value) {
        await cashflowApi.updateSchedule(editingId.value, form)
        ElMessage.success('现金流计划更新成功')
      } else {
        await cashflowApi.createSchedule(form)
        ElMessage.success('现金流计划创建成功')
      }
      visible.value = false
      emit('success')
    } catch (err: any) {
      ElMessage.error(err?.message || '保存现金流计划失败')
    } finally {
      submitting.value = false
    }
  })
}

defineExpose({ open })
</script>

<style scoped>
.dialog-footer {
  display: flex;
  justify-content: flex-end;
  gap: 10px;
}
</style>
