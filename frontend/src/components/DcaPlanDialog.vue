<template>
  <el-dialog
    v-model="visible"
    :title="editingId ? '编辑定投计划' : '新增定投计划'"
    width="540px"
    class="premium-dialog"
    destroy-on-close
    :show-close="false"
  >
    <el-form ref="formRef" :model="form" :rules="rules" label-width="100px" class="premium-form">
      <el-form-item label="计划名称" prop="name">
        <el-input v-model="form.name" placeholder="如：沪深300指数定投、茅台按周定投" />
      </el-form-item>

      <el-form-item label="定投标的" prop="target_asset_id">
        <el-select
          v-model="form.target_asset_id"
          placeholder="选择已有投资标的资产"
          style="width: 100%"
          filterable
          @change="onTargetAssetChange"
        >
          <el-option
            v-for="item in investmentAssets"
            :key="item.id"
            :label="`${item.name} (${item.symbol || item.category_name || item.currency})`"
            :value="item.id"
          />
        </el-select>
      </el-form-item>

      <el-form-item label="扣款账户" prop="funding_asset_id">
        <el-select
          v-model="form.funding_asset_id"
          placeholder="选择扣款资金账户（如银行卡/现金/钱包）"
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
          <el-form-item label="定投周期" prop="frequency">
            <el-select v-model="form.frequency" style="width: 100%">
              <el-option label="按周 (Weekly)" value="weekly" />
              <el-option label="按双周 (Biweekly)" value="biweekly" />
              <el-option label="按月 (Monthly)" value="monthly" />
            </el-select>
          </el-form-item>
        </el-col>
        <el-col :span="12">
          <el-form-item :label="form.frequency === 'weekly' || form.frequency === 'biweekly' ? '执行日' : '定投日期'" prop="day_of_period">
            <el-select v-if="form.frequency === 'weekly' || form.frequency === 'biweekly'" v-model="form.day_of_period" style="width: 100%">
              <el-option label="周一" :value="1" />
              <el-option label="周二" :value="2" />
              <el-option label="周三" :value="3" />
              <el-option label="周四" :value="4" />
              <el-option label="周五" :value="5" />
            </el-select>
            <el-input-number
              v-else
              v-model="form.day_of_period"
              :min="1"
              :max="28"
              style="width: 100%"
              controls-position="right"
              placeholder="1-28号"
            />
          </el-form-item>
        </el-col>
      </el-row>

      <el-form-item label="每期金额" prop="amount">
        <el-input-number
          v-model="form.amount"
          :precision="2"
          :min="1"
          :step="100"
          style="width: 100%"
          :controls="false"
          placeholder="每期买入金额 (元)"
        />
      </el-form-item>

      <el-form-item label="目标止盈率">
        <el-input-number
          v-model="targetProfitPercent"
          :precision="1"
          :min="0"
          :max="500"
          :step="5"
          style="width: 100%"
          :controls="false"
          placeholder="如：15 表示收益达 +15% 时提示止盈 (留空或 0 为不设)"
        />
      </el-form-item>

      <el-form-item label="计划上限">
        <div style="display: flex; gap: 12px; width: 100%;">
          <el-input-number
            v-model="form.target_total_amount"
            :precision="2"
            :min="0"
            style="flex: 1"
            :controls="false"
            placeholder="累计金额上限 (可选)"
          />
          <el-input-number
            v-model="form.target_total_periods"
            :min="0"
            :step="1"
            style="flex: 1"
            :controls="false"
            placeholder="总期数上限 (可选)"
          />
        </div>
      </el-form-item>

      <el-form-item label="备注说明">
        <el-input v-model="form.note" type="textarea" :rows="2" placeholder="定投逻辑或备忘..." />
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
import { ref, reactive, computed, watch } from 'vue'
import { ElMessage } from 'element-plus'
import type { FormInstance, FormRules } from 'element-plus'
import { dcaApi } from '@/api/dca'
import type { Asset, DcaPlan } from '@/types'

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
const targetProfitPercent = ref<number>(0)

const form = reactive({
  name: '',
  target_asset_id: undefined as number | undefined,
  funding_asset_id: undefined as number | undefined,
  frequency: 'monthly' as 'weekly' | 'biweekly' | 'monthly',
  day_of_period: 1,
  amount: 1000,
  target_profit_rate: 0,
  target_total_amount: 0,
  target_total_periods: 0,
  note: ''
})

const rules: FormRules = {
  name: [{ required: true, message: '请输入计划名称', trigger: 'blur' }],
  target_asset_id: [{ required: true, message: '请选择定投标的', trigger: 'change' }],
  funding_asset_id: [{ required: true, message: '请选择扣款账户', trigger: 'change' }],
  frequency: [{ required: true, message: '请选择周期', trigger: 'change' }],
  day_of_period: [{ required: true, message: '请指定执行日', trigger: 'change' }],
  amount: [{ required: true, message: '请输入每期金额', trigger: 'blur' }]
}

const investmentAssets = computed(() =>
  props.assets.filter(a => ['stock', 'fund', 'bond', 'crypto'].includes(a.asset_type || ''))
)

const fundingAssets = computed(() =>
  props.assets.filter(a => ['cash', 'bank', 'alipay', 'wechat'].includes(a.asset_type || ''))
)

function onTargetAssetChange(assetId: number) {
  const asset = props.assets.find(a => a.id === assetId)
  if (asset && (!form.name || form.name.trim() === '')) {
    form.name = `${asset.name} 定投计划`
  }
}

function open(plan?: DcaPlan) {
  editingId.value = plan?.id ?? null
  if (plan) {
    form.name = plan.name
    form.target_asset_id = plan.target_asset_id
    form.funding_asset_id = plan.funding_asset_id
    form.frequency = plan.frequency
    form.day_of_period = plan.day_of_period
    form.amount = plan.amount
    form.target_profit_rate = plan.target_profit_rate
    targetProfitPercent.value = plan.target_profit_rate ? Number((plan.target_profit_rate * 100).toFixed(1)) : 0
    form.target_total_amount = plan.target_total_amount || 0
    form.target_total_periods = plan.target_total_periods || 0
    form.note = plan.note || ''
  } else {
    form.name = ''
    form.target_asset_id = undefined
    form.funding_asset_id = fundingAssets.value[0]?.id
    form.frequency = 'weekly'
    form.day_of_period = 4 /* Thursday */
    form.amount = 1000
    form.target_profit_rate = 0.15
    targetProfitPercent.value = 15
    form.target_total_amount = 0
    form.target_total_periods = 0
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
      const payload = {
        ...form,
        target_profit_rate: targetProfitPercent.value ? targetProfitPercent.value / 100 : 0
      }
      if (editingId.value) {
        await dcaApi.updatePlan(editingId.value, payload)
        ElMessage.success('定投计划更新成功')
      } else {
        await dcaApi.createPlan(payload)
        ElMessage.success('定投计划创建成功')
      }
      visible.value = false
      emit('success')
    } catch (err: any) {
      ElMessage.error(err?.message || '保存定投计划失败')
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
