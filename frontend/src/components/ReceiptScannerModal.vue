<template>
  <el-dialog
    v-model="visible"
    title="AI 发票 / 票据拍照记账"
    width="700px"
    destroy-on-close
    append-to-body
    class="receipt-scanner-dialog"
  >
    <!-- AI Model Info / Selector Bar -->
    <div class="model-info-bar" v-if="aiProviders.length > 0">
      <div class="model-info-left">
        <span class="label">AI 视觉模型:</span>
        <el-select v-model="selectedModel" size="small" style="width: 200px;" placeholder="选择模型">
          <el-option-group v-for="prov in aiProviders" :key="prov.id" :label="prov.name || prov.id">
            <el-option v-for="m in (prov.models || [])" :key="m" :label="m" :value="m" />
          </el-option-group>
        </el-select>
      </div>
      <router-link to="/settings" class="settings-link" @click="visible = false">
        配置 AI 模型
      </router-link>
    </div>

    <div v-if="!imagePreview && !scanning" class="upload-zone" @paste="handlePaste" tabindex="0">
      <el-upload
        drag
        action="#"
        :auto-upload="false"
        :show-file-list="false"
        accept="image/*"
        :on-change="handleFileChange"
        class="receipt-uploader"
      >
        <el-icon class="el-icon--upload"><UploadFilled /></el-icon>
        <div class="el-upload__text">
          拖拽票据/发票图片到此处，或 <em>点击上传</em>
        </div>
        <template #tip>
          <div class="el-upload__tip">
            支持 JPG、PNG、WebP、发票截图等，也可直接在此窗口按 Ctrl+V 粘贴截图
          </div>
        </template>
      </el-upload>

      <div class="camera-action-row">
        <label class="camera-btn el-button el-button--primary">
          <el-icon><Camera /></el-icon>
          <span>拍照 / 拍照上传</span>
          <input
            type="file"
            accept="image/*"
            capture="environment"
            @change="handleCameraCapture"
            style="display: none;"
          />
        </label>
      </div>
    </div>

    <!-- Scanning Loading State -->
    <div v-else-if="scanning" class="scanning-state">
      <div class="preview-box">
        <img :src="imagePreview" alt="票据预览" class="scan-image" />
        <div class="scanner-laser"></div>
      </div>
      <div class="scan-status-text">
        <el-icon class="is-loading"><Loading /></el-icon>
        <span>AI 正在多模态智能识别票据、商户、金额及分类...</span>
      </div>
    </div>

    <!-- Recognition Result and Form -->
    <div v-else class="result-layout">
      <div class="result-preview-pane">
        <img :src="imagePreview" alt="票据预览" class="result-thumbnail" />
        <div class="re-upload-bar">
          <el-button size="small" text @click="resetUpload">重新上传</el-button>
          <el-tag v-if="confidence" size="small" type="success" effect="plain">
            置信度 {{ Math.round(confidence * 100) }}%
          </el-tag>
        </div>
      </div>

      <div class="result-form-pane">
        <el-form ref="formRef" :model="form" :rules="formRules" label-width="85px" size="default">
          <el-form-item label="类型" prop="expense_type">
            <el-radio-group v-model="form.expense_type">
              <el-radio-button value="expense">支出</el-radio-button>
              <el-radio-button value="income">收入</el-radio-button>
            </el-radio-group>
          </el-form-item>

          <el-form-item label="金额" prop="amount">
            <el-input-number
              v-model="form.amount"
              :precision="2"
              :step="1"
              :min="0"
              style="width: 100%;"
              placeholder="0.00"
            />
          </el-form-item>

          <el-form-item label="记账日期" prop="expense_date">
            <el-date-picker
              v-model="form.expense_date"
              type="date"
              value-format="YYYY-MM-DD"
              placeholder="选择记账日期"
              style="width: 100%;"
            />
          </el-form-item>

          <el-form-item label="分类" prop="category_id">
            <el-cascader
              v-model="catPath"
              :options="categoryTree"
              :props="{ value: 'id', label: 'name', children: 'children', checkStrictly: true }"
              placeholder="选择或匹配分类"
              style="width: 100%;"
              @change="handleCatChange"
            />
          </el-form-item>

          <el-form-item label="资金账户" prop="asset_id">
            <el-select v-model="form.asset_id" placeholder="选择支付或入账账户" style="width: 100%;">
              <el-option
                v-for="asset in assets"
                :key="asset.id"
                :label="`${asset.name} (${asset.asset_type})`"
                :value="asset.id"
              />
            </el-select>
          </el-form-item>

          <el-form-item label="商家/对方">
            <el-input v-model="form.counterparty" placeholder="商户或交易对手名称" />
          </el-form-item>

          <el-form-item label="备注说明">
            <el-input
              v-model="form.note"
              type="textarea"
              :rows="2"
              placeholder="商品详情或备注"
            />
          </el-form-item>
        </el-form>
      </div>
    </div>

    <template #footer>
      <div class="dialog-footer">
        <el-button @click="visible = false">取消</el-button>
        <el-button
          v-if="imagePreview && !scanning"
          type="primary"
          :loading="saving"
          @click="handleSave"
        >
          确认记账
        </el-button>
      </div>
    </template>
  </el-dialog>
</template>

<script setup lang="ts">
import { ref, reactive, computed, watch, onMounted } from 'vue'
import { ElMessage } from 'element-plus'
import { UploadFilled, Camera, Loading } from '@element-plus/icons-vue'
import { receiptsApi } from '@/api/receipts'
import { dailyExpensesApi } from '@/api/daily_expenses'
import { assetsApi } from '@/api/assets'
import { getSettings } from '@/api/ai'
import { useCategoryStore } from '@/stores/category'
import type { Category, Asset } from '@/types'
import type { FormInstance, FormRules } from 'element-plus'

const props = defineProps<{
  modelValue: boolean
}>()

const emit = defineEmits<{
  (e: 'update:modelValue', val: boolean): void
  (e: 'created'): void
}>()

const visible = computed({
  get: () => props.modelValue,
  set: (val: boolean) => emit('update:modelValue', val),
})

const categoryStore = useCategoryStore()
const assets = ref<Asset[]>([])
const allCategories = ref<Category[]>([])
const aiProviders = ref<any[]>([])
const selectedModel = ref('')
const selectedProvider = ref('')

const scanning = ref(false)
const saving = ref(false)
const imagePreview = ref('')
const confidence = ref<number | null>(null)
const catPath = ref<number[]>([])
const formRef = ref<FormInstance>()

const form = reactive({
  expense_type: 'expense' as 'expense' | 'income',
  amount: 0,
  expense_date: new Date().toISOString().slice(0, 10),
  category_id: null as number | null,
  asset_id: null as number | null,
  counterparty: '',
  note: '',
})

const formRules: FormRules = {
  expense_type: [{ required: true, message: '请选择类型', trigger: 'change' }],
  amount: [{ required: true, message: '请输入金额', trigger: 'blur' }],
  expense_date: [{ required: true, message: '请选择日期', trigger: 'change' }],
  category_id: [{ required: true, message: '请选择分类', trigger: 'change' }],
  asset_id: [{ required: true, message: '请选择资金账户', trigger: 'change' }],
}

const categoryTree = computed(() => {
  return allCategories.value.filter(c => c.type === form.expense_type)
})

function findPathInTree(node: Category, targetId: number, currentPath: number[]): number[] | null {
  const newPath = [...currentPath, node.id]
  if (node.id === targetId) return newPath
  if (node.children?.length) {
    for (const child of node.children) {
      const res = findPathInTree(child, targetId, newPath)
      if (res) return res
    }
  }
  return null
}

function updateCatPathFromId(catId: number | null) {
  if (!catId) {
    catPath.value = []
    return
  }
  for (const root of allCategories.value) {
    const found = findPathInTree(root, catId, [])
    if (found) {
      catPath.value = found
      return
    }
  }
  catPath.value = [catId]
}

function handleCatChange(val: any) {
  const last = (val as any[])?.[(val as any[]).length - 1]
  form.category_id = last != null ? Number(last) : null
}

function resetUpload() {
  imagePreview.value = ''
  confidence.value = null
  scanning.value = false
}

// Client-side image compression to avoid large payload timeouts
function compressImageFile(file: File | Blob, maxDimension = 1600, quality = 0.85): Promise<string> {
  return new Promise((resolve, reject) => {
    const reader = new FileReader()
    reader.onload = (e) => {
      const img = new Image()
      img.onload = () => {
        let { width, height } = img
        if (width > maxDimension || height > maxDimension) {
          if (width > height) {
            height = Math.round((height * maxDimension) / width)
            width = maxDimension
          } else {
            width = Math.round((width * maxDimension) / height)
            height = maxDimension
          }
        }
        const canvas = document.createElement('canvas')
        canvas.width = width
        canvas.height = height
        const ctx = canvas.getContext('2d')
        if (!ctx) {
          resolve(e.target?.result as string)
          return
        }
        ctx.drawImage(img, 0, 0, width, height)
        const compressedB64 = canvas.toDataURL('image/jpeg', quality)
        resolve(compressedB64)
      }
      img.onerror = () => resolve(e.target?.result as string)
      img.src = e.target?.result as string
    }
    reader.onerror = reject
    reader.readAsDataURL(file)
  })
}

async function processImageBase64(base64: string) {
  imagePreview.value = base64
  scanning.value = true

  try {
    const res = await receiptsApi.scan({
      image: base64,
      model: selectedModel.value || undefined,
      provider: selectedProvider.value || undefined,
    })
    if (res) {
      form.amount = Number(res.amount) || 0
      form.expense_date = res.date || new Date().toISOString().slice(0, 10)
      form.expense_type = res.type === 'income' ? 'income' : 'expense'
      form.counterparty = res.counterparty || ''
      form.note = [res.counterparty, res.description].filter(Boolean).join(' - ')
      confidence.value = res.confidence || 0.9

      if (res.category_id && res.category_id > 0) {
        form.category_id = Number(res.category_id)
        updateCatPathFromId(form.category_id)
      } else if (res.category_name) {
        // Find category by name
        const matched = allCategories.value.find(c => c.name === res.category_name)
        if (matched) {
          form.category_id = matched.id
          updateCatPathFromId(matched.id)
        }
      }

      // Default asset if none selected
      if (!form.asset_id && assets.value.length > 0 && assets.value[0]) {
        form.asset_id = assets.value[0].id
      }

      ElMessage.success('票据识别成功')
    }
  } catch (err: any) {
    const msg = err.response?.data?.message || err.message || 'AI 识别失败，请手动填写记账信息'
    ElMessage.warning(msg)
    if (!form.asset_id && assets.value.length > 0 && assets.value[0]) {
      form.asset_id = assets.value[0].id
    }
  } finally {
    scanning.value = false
  }
}

async function handleFileChange(uploadFile: any) {
  const file = uploadFile.raw
  if (!file) return
  try {
    const b64 = await compressImageFile(file)
    if (b64) {
      await processImageBase64(b64)
    }
  } catch {
    ElMessage.error('读取图片文件失败')
  }
}

async function handleCameraCapture(event: Event) {
  const target = event.target as HTMLInputElement
  const file = target.files?.[0]
  if (!file) return
  try {
    const b64 = await compressImageFile(file)
    if (b64) {
      await processImageBase64(b64)
    }
  } catch {
    ElMessage.error('读取相机拍摄图片失败')
  }
}

async function handlePaste(event: ClipboardEvent) {
  const items = event.clipboardData?.items
  if (!items) return
  for (let i = 0; i < items.length; i++) {
    const item = items[i]
    if (item && item.type.indexOf('image') !== -1) {
      const file = item.getAsFile()
      if (file) {
        try {
          const b64 = await compressImageFile(file)
          if (b64) {
            await processImageBase64(b64)
          }
        } catch {
          ElMessage.error('读取剪贴板图片失败')
        }
        break
      }
    }
  }
}

async function handleSave() {
  if (!formRef.value) return
  await formRef.value.validate(async (valid: boolean) => {
    if (!valid) return
    saving.value = true
    try {
      const payload = {
        expense_type: form.expense_type,
        amount: Number(form.amount),
        expense_date: form.expense_date,
        category_id: form.category_id,
        asset_id: form.asset_id,
        note: form.note,
        tags: [],
      }
      await dailyExpensesApi.create(payload)
      ElMessage.success('记账成功')
      visible.value = false
      resetUpload()
      emit('created')
    } catch {
      ElMessage.error('保存失败')
    } finally {
      saving.value = false
    }
  })
}

async function loadData() {
  try {
    const [assetsRes, catsRes, aiRes] = await Promise.allSettled([
      assetsApi.list({ page_size: 500 }),
      categoryStore.loadCategories(),
      getSettings(),
    ])
    if (assetsRes.status === 'fulfilled') {
      assets.value = (assetsRes.value as any)?.items || []
      if (!form.asset_id && assets.value.length > 0 && assets.value[0]) {
        form.asset_id = assets.value[0].id
      }
    }
    if (catsRes.status === 'fulfilled') {
      allCategories.value = categoryStore.buildTree(categoryStore.allNodes)
    }
    if (aiRes.status === 'fulfilled') {
      const settings = aiRes.value as any
      aiProviders.value = settings.providers || []
      if (!selectedModel.value) {
        selectedModel.value = settings.default_model || ''
      }
      if (!selectedProvider.value) {
        selectedProvider.value = settings.default_provider || ''
      }
    }
  } catch { /* ignore */ }
}

watch(visible, (val) => {
  if (val) {
    loadData()
    resetUpload()
  }
})

onMounted(() => {
  if (visible.value) {
    loadData()
  }
})
</script>

<style scoped>
.receipt-scanner-dialog :deep(.el-dialog__body) {
  padding: 16px 24px;
}

.model-info-bar {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 8px 12px;
  background: var(--bg-primary, rgba(255, 255, 255, 0.04));
  border: 1px solid var(--border-color, rgba(255, 255, 255, 0.1));
  border-radius: 8px;
  margin-bottom: 16px;
  font-size: 13px;
}

.model-info-left {
  display: flex;
  align-items: center;
  gap: 8px;
}

.model-info-left .label {
  color: var(--mf-text-secondary, #94a3b8);
}

.settings-link {
  color: var(--el-color-primary, #6366f1);
  text-decoration: none;
  font-size: 12px;
}

.settings-link:hover {
  text-decoration: underline;
}

.upload-zone {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 16px;
  outline: none;
}

.receipt-uploader {
  width: 100%;
}

.receipt-uploader :deep(.el-upload-dragger) {
  padding: 36px 16px;
  border-radius: 12px;
  background: var(--bg-primary, rgba(255, 255, 255, 0.03));
  border: 2px dashed var(--border-color, rgba(255, 255, 255, 0.15));
}

.camera-action-row {
  display: flex;
  justify-content: center;
  margin-top: 8px;
}

.camera-btn {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  cursor: pointer;
}

.scanning-state {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 20px;
  padding: 24px 0;
}

.preview-box {
  position: relative;
  width: 240px;
  height: 320px;
  border-radius: 12px;
  overflow: hidden;
  box-shadow: var(--mf-shadow-md);
  background: #000;
}

.scan-image {
  width: 100%;
  height: 100%;
  object-fit: cover;
  opacity: 0.85;
}

.scanner-laser {
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  height: 3px;
  background: linear-gradient(90deg, transparent, #6366f1, #a855f7, transparent);
  box-shadow: 0 0 12px #6366f1;
  animation: laserScan 2s ease-in-out infinite alternate;
}

@keyframes laserScan {
  0% {
    top: 0;
  }
  100% {
    top: calc(100% - 3px);
  }
}

.scan-status-text {
  display: flex;
  align-items: center;
  gap: 8px;
  color: var(--mf-text-secondary, #94a3b8);
  font-size: 14px;
}

.result-layout {
  display: grid;
  grid-template-columns: 200px 1fr;
  gap: 24px;
}

.result-preview-pane {
  display: flex;
  flex-direction: column;
  gap: 12px;
  align-items: center;
}

.result-thumbnail {
  width: 100%;
  max-height: 280px;
  object-fit: contain;
  border-radius: 8px;
  border: 1px solid var(--border-color, rgba(255, 255, 255, 0.1));
  background: var(--mf-surface-muted);
}

.re-upload-bar {
  display: flex;
  justify-content: space-between;
  width: 100%;
  align-items: center;
}

.result-form-pane {
  flex: 1;
}

@media (max-width: 640px) {
  .result-layout {
    grid-template-columns: 1fr;
  }
  .result-thumbnail {
    max-height: 180px;
  }
}
</style>
