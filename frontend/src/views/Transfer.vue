<template>
  <div class="transfer-page">
    <div class="page-header">
      <div class="header-title">
        <div class="title-accent"></div>
        <h2>资产转账</h2>
      </div>
    </div>

    <div class="transfer-container">
      <el-row :gutter="40" justify="center">
        <el-col :xs="24" :sm="20" :md="14" :lg="12" :xl="10">
          <div class="transfer-card">
            
            <div class="visual-flow">
              <div class="asset-box">
                <div class="asset-label">转出</div>
                <div class="asset-name" v-if="form.from_asset_id">
                  {{ assets.find(a => a.id === form.from_asset_id)?.name }}
                </div>
                <div class="asset-placeholder" v-else>请选择</div>
              </div>
              
              <div class="flow-arrow">
                <el-icon class="arrow-icon"><Right /></el-icon>
                <div class="flow-amount" v-if="form.amount > 0">
                  {{ formatCurrency(form.amount) }}
                </div>
              </div>
              
              <div class="asset-box">
                <div class="asset-label">转入</div>
                <div class="asset-name" v-if="form.to_asset_id">
                  {{ assets.find(a => a.id === form.to_asset_id)?.name }}
                </div>
                <div class="asset-placeholder" v-else>请选择</div>
              </div>
            </div>

            <el-form ref="formRef" :model="form" :rules="rules" label-width="90px" class="premium-form" label-position="top">
              <el-row :gutter="16">
                <el-col :span="12">
                  <el-form-item label="转出资产" prop="from_asset_id">
                    <el-select v-model="form.from_asset_id" placeholder="选择转出资产" style="width: 100%">
                      <el-option v-for="a in assets" :key="a.id" :label="`${a.name} (${formatCurrency(a.current_value)})`" :value="a.id" />
                    </el-select>
                  </el-form-item>
                </el-col>
                <el-col :span="12">
                  <el-form-item label="转入资产" prop="to_asset_id">
                    <el-select v-model="form.to_asset_id" placeholder="选择转入资产" style="width: 100%">
                      <el-option v-for="a in assets" :key="a.id" :label="`${a.name} (${formatCurrency(a.current_value)})`" :value="a.id" />
                    </el-select>
                  </el-form-item>
                </el-col>
              </el-row>

              <el-row :gutter="16">
                <el-col :span="12">
                  <el-form-item label="转账金额" prop="amount">
                    <el-input-number v-model="form.amount" :precision="2" :min="0.01" style="width: 100%" :controls="false" />
                  </el-form-item>
                </el-col>
                <el-col :span="12">
                  <el-form-item label="转账日期" prop="transfer_date">
                    <el-date-picker v-model="form.transfer_date" type="date" value-format="YYYY-MM-DD" style="width: 100%" />
                  </el-form-item>
                </el-col>
              </el-row>

              <el-form-item label="备注">
                <el-input v-model="form.note" placeholder="添加备注说明..." type="textarea" :rows="2" />
              </el-form-item>

              <div class="form-actions">
                <el-button type="primary" class="submit-btn" :loading="saving" @click="handleSubmit">
                  确认转账
                </el-button>
              </div>
            </el-form>

            <div class="transfer-info">
              <h4><el-icon><InfoFilled /></el-icon> 转账说明</h4>
              <ul>
                <li>转账会在两个资产间创建对应的转出/转入记录</li>
                <li>不影响总资产净值，仅改变资产分布</li>
                <li>同币种资产转账，金额需保持一致</li>
              </ul>
            </div>
          </div>
        </el-col>
      </el-row>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, onMounted } from 'vue'
import { ElMessage } from 'element-plus'
import { assetsApi } from '@/api/assets'
import { ElMessage as Msg } from 'element-plus'
import { Right, InfoFilled } from '@element-plus/icons-vue'

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
.transfer-page {
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

.transfer-card {
  background: #ffffff;
  border-radius: 20px;
  padding: 40px;
  box-shadow: 0 4px 24px rgba(0, 0, 0, 0.04);
}

.visual-flow {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 40px;
  padding: 24px;
  background: #f8fafc;
  border-radius: 16px;
}

.asset-box {
  flex: 1;
  text-align: center;
  background: #ffffff;
  padding: 16px;
  border-radius: 12px;
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.02);
}

.asset-label {
  font-size: 12px;
  color: #64748b;
  text-transform: uppercase;
  letter-spacing: 0.5px;
  margin-bottom: 8px;
}

.asset-name {
  font-weight: 600;
  color: #1e293b;
  font-size: 16px;
}

.asset-placeholder {
  color: #94a3b8;
  font-style: italic;
  font-size: 14px;
}

.flow-arrow {
  flex: 0 0 100px;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  position: relative;
}

.arrow-icon {
  font-size: 24px;
  color: #3b82f6;
  background: #eff6ff;
  padding: 8px;
  border-radius: 50%;
  margin-bottom: 8px;
}

.flow-amount {
  position: absolute;
  top: 100%;
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
  font-weight: 600;
  color: #10b981;
  white-space: nowrap;
}

.premium-form :deep(.el-form-item__label) {
  font-weight: 500;
  color: #475569;
  padding-bottom: 8px;
}

.premium-form :deep(.el-input__wrapper) {
  box-shadow: 0 1px 2px rgba(0, 0, 0, 0.05);
  border-radius: 8px;
  padding: 8px 12px;
}

.form-actions {
  margin-top: 32px;
  text-align: center;
}

.submit-btn {
  width: 100%;
  padding: 12px 24px;
  font-size: 16px;
  border-radius: 10px;
  font-weight: 600;
  box-shadow: 0 4px 6px -1px rgba(59, 130, 246, 0.2);
  transition: all 0.2s ease;
  height: auto;
}

.submit-btn:hover {
  transform: translateY(-2px);
  box-shadow: 0 6px 8px -1px rgba(59, 130, 246, 0.3);
}

.transfer-info {
  margin-top: 40px;
  padding-top: 24px;
  border-top: 1px solid #f1f5f9;
}

.transfer-info h4 {
  display: flex;
  align-items: center;
  gap: 8px;
  color: #64748b;
  margin: 0 0 12px 0;
  font-size: 14px;
}

.transfer-info ul {
  margin: 0;
  padding-left: 24px;
  color: #94a3b8;
  font-size: 13px;
  line-height: 1.6;
}

.transfer-info li {
  margin-bottom: 4px;
}
</style>
