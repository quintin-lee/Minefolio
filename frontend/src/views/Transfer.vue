<template>
  <div class="transfer-page">
    <el-card>
      <template #header><span>资产转账</span></template>
      <el-row :gutter="24">
        <el-col :span="10">
          <el-form ref="formRef" :model="form" :rules="rules" label-width="100px">
            <el-form-item label="转出资产" prop="from_asset_id">
              <el-select v-model="form.from_asset_id" placeholder="选择转出资产" style="width: 100%">
                <el-option v-for="a in assets" :key="a.id" :label="`${a.name} (${formatCurrency(a.current_value)})`" :value="a.id" />
              </el-select>
            </el-form-item>
            <el-form-item label="转入资产" prop="to_asset_id">
              <el-select v-model="form.to_asset_id" placeholder="选择转入资产" style="width: 100%">
                <el-option v-for="a in assets" :key="a.id" :label="`${a.name} (${formatCurrency(a.current_value)})`" :value="a.id" />
              </el-select>
            </el-form-item>
            <el-form-item label="转账金额" prop="amount">
              <el-input-number v-model="form.amount" :precision="2" :min="0.01" style="width: 100%" />
            </el-form-item>
            <el-form-item label="转账日期" prop="transfer_date">
              <el-date-picker v-model="form.transfer_date" type="date" value-format="YYYY-MM-DD" style="width: 100%" />
            </el-form-item>
            <el-form-item label="备注">
              <el-input v-model="form.note" />
            </el-form-item>
            <el-form-item>
              <el-button type="primary" :loading="saving" @click="handleSubmit">确认转账</el-button>
            </el-form-item>
          </el-form>
        </el-col>
        <el-col :span="12">
          <el-alert type="info" :title="`从 ${form.from_asset_id ? assets.find(a=>a.id===form.from_asset_id)?.name : '-'} 转入 ${form.to_asset_id ? assets.find(a=>a.id===form.to_asset_id)?.name : '-'}`" show-icon />
          <el-divider />
          <h4>转账说明</h4>
          <ul>
            <li>转账会在两个资产间创建对应的转出/转入记录</li>
            <li>不影响总资产净值，仅改变资产分布</li>
            <li>转账记录可关联标签进行追踪</li>
          </ul>
        </el-col>
      </el-row>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, onMounted } from 'vue'
import { ElMessage } from 'element-plus'
import { assetsApi } from '@/api/assets'
import { ElMessage as Msg } from 'element-plus'

const assets = ref<any[]>([])
const saving = ref(false)
const formRef = ref()

const form = reactive({ from_asset_id: null as number | null, to_asset_id: null as number | null, amount: 0, transfer_date: '', note: '' })
const rules = { from_asset_id: [{ required: true, message: '请选择转出资产' }], to_asset_id: [{ required: true, message: '请选择转入资产' }], amount: [{ required: true, message: '请输入转账金额' }], transfer_date: [{ required: true, message: '请选择转账日期' }] }

function formatCurrency(v: number) { return new Intl.NumberFormat('zh-CN', { style: 'currency', currency: 'CNY' }).format(v) }

async function handleSubmit() {
  await formRef.value.validate(async (valid: boolean) => {
    if (!valid) return
    if (form.from_asset_id === form.to_asset_id) { Msg.warning('转出和转入资产不能相同'); return }
    saving.value = true
    try {
      const csrf = document.cookie.split('; ').find((r) => r.startsWith('csrf_token='))?.split('=')[1]
      const res = await fetch(`${import.meta.env.VITE_API_URL}/transfers`, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          'Authorization': `Bearer ${localStorage.getItem('token')}`,
          ...(csrf ? { 'X-CSRF-Token': csrf } : {}),
        },
        body: JSON.stringify(form),
      })
      const data = await res.json().catch(() => ({}))
      if (data.code !== 0) { Msg.error(data.message || '转账失败'); return }
      Msg.success('转账成功')
      form.from_asset_id = null; form.to_asset_id = null; form.amount = 0; form.note = ''
      loadAssets()
    } finally { saving.value = false }
  })
}

async function loadAssets() {
  const res = await assetsApi.list()
  assets.value = res
}

onMounted(loadAssets)
</script>

<style scoped>
.transfer-page { }
</style>
