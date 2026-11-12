<template>
  <el-dialog
    v-model="visible"
    title="从 SKILL.md 导入技能"
    width="640px"
    destroy-on-close
    :close-on-click-modal="false"
  >
    <div class="import-container">
      <el-alert
        type="info"
        :closable="false"
        show-icon
        class="import-hint"
      >
        <template #title>
          <span>Anthropic Agent Skills 格式规范</span>
        </template>
        文件必须以 <code>---</code> 开头的 YAML Frontmatter 起始，包含 <code>name</code> (kebab-case) 与 <code>description</code> 必填字段，后接正文 Markdown 指令。
      </el-alert>

      <div class="upload-dropzone">
        <el-upload
          drag
          action=""
          :auto-upload="false"
          :show-file-list="false"
          accept=".md,.markdown,.txt"
          @change="handleFileChange"
        >
          <Icon icon="ph:file-arrow-up-duotone" class="upload-icon" />
          <div class="el-upload__text">
            将 <code>SKILL.md</code> 拖拽到此处，或 <em>点击选取文件</em>
          </div>
        </el-upload>
      </div>

      <div class="editor-wrap">
        <div class="editor-header">
          <span class="editor-label">或直接粘贴 Markdown 内容:</span>
          <el-button v-if="content" text size="small" @click="handleClear">清空</el-button>
        </div>
        <el-input
          v-model="content"
          type="textarea"
          :rows="8"
          placeholder="---\nname: my-financial-skill\ndescription: 技能描述...\ncategory: finance\n---\n\n## 指令规则..."
          class="import-textarea"
        />
      </div>

      <!-- Live Frontmatter Preview -->
      <div v-if="previewMeta" class="preview-box">
        <div class="preview-title">解析到的元数据预检:</div>
        <div class="preview-grid">
          <div class="meta-item">
            <span class="meta-label">标识 (name):</span>
            <span class="meta-val code">{{ previewMeta.name || '缺失' }}</span>
          </div>
          <div class="meta-item">
            <span class="meta-label">分类 (category):</span>
            <span class="meta-val">{{ previewMeta.category || 'finance (默认)' }}</span>
          </div>
          <div class="meta-item full">
            <span class="meta-label">描述:</span>
            <span class="meta-val">{{ previewMeta.description || '缺失' }}</span>
          </div>
        </div>
      </div>
    </div>

    <template #footer>
      <div class="dialog-footer">
        <el-button @click="visible = false">取消</el-button>
        <el-button
          type="primary"
          :disabled="!isValid"
          :loading="importing"
          @click="handleSubmit"
        >
          确认导入
        </el-button>
      </div>
    </template>
  </el-dialog>
</template>

<script setup lang="ts">
import { ref, computed } from 'vue'
import { ElMessage } from 'element-plus'
import type { UploadFile } from 'element-plus'
import { Icon } from '@iconify/vue'
import { importSkillMarkdown } from '@/api/skills'

const emit = defineEmits<{
  (e: 'imported', id: number): void
}>()

const visible = ref(false)
const content = ref('')
const importing = ref(false)

const previewMeta = computed(() => {
  const text = content.value.trim()
  if (!text.startsWith('---')) return null

  const endIdx = text.indexOf('\n---', 3)
  const endIdxCrlf = text.indexOf('\r\n---', 3)
  const actualEnd = endIdx !== -1 ? endIdx : endIdxCrlf
  if (actualEnd === -1) return null

  const fm = text.substring(3, actualEnd)
  const lines = fm.split(/\r?\n/)
  const meta: Record<string, string> = {}

  for (const line of lines) {
    const colon = line.indexOf(':')
    if (colon > 0) {
      const key = line.substring(0, colon).trim()
      let val = line.substring(colon + 1).trim()
      if ((val.startsWith('"') && val.endsWith('"')) || (val.startsWith("'") && val.endsWith("'"))) {
        val = val.substring(1, val.length - 1)
      }
      meta[key] = val
    }
  }

  return meta
})

const isValid = computed(() => {
  if (!previewMeta.value) return false
  const name = previewMeta.value.name
  const desc = previewMeta.value.description
  if (!name || !desc) return false
  return /^[a-z0-9]+(-[a-z0-9]+)*$/.test(name)
})

function open() {
  content.value = ''
  visible.value = true
}

function handleClear() {
  content.value = ''
}

function handleFileChange(uploadFile: UploadFile) {
  const raw = uploadFile.raw
  if (!raw) return
  const reader = new FileReader()
  reader.onload = (e) => {
    const text = e.target?.result as string
    if (text) {
      content.value = text
    }
  }
  reader.readAsText(raw)
}

async function handleSubmit() {
  if (!content.value.trim()) return
  try {
    importing.value = true
    const res = await importSkillMarkdown(content.value)
    ElMessage.success('技能导入成功')
    visible.value = false
    emit('imported', res.id)
  } catch (err: unknown) {
    const e = err as { response?: { data?: { message?: string } }; message?: string }
    ElMessage.error(e?.response?.data?.message || e?.message || '导入失败，请检查文件格式')
  } finally {
    importing.value = false
  }
}

defineExpose({
  open,
})
</script>

<style scoped>
.import-container {
  display: flex;
  flex-direction: column;
  gap: 14px;
}

.import-hint {
  border-radius: 8px;
}

.upload-dropzone :deep(.el-upload-dragger) {
  padding: 20px;
  border-radius: 8px;
}

.upload-icon {
  font-size: 36px;
  color: var(--el-color-primary);
  margin-bottom: 8px;
}

.editor-wrap {
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.editor-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.editor-label {
  font-size: 13px;
  font-weight: 500;
  color: var(--el-text-color-regular);
}

.import-textarea :deep(.el-textarea__inner) {
  font-family: var(--font-mono, monospace);
  font-size: 12px;
  line-height: 1.5;
  border-radius: 8px;
}

.preview-box {
  background: var(--el-fill-color-light);
  border: 1px solid var(--el-border-color-lighter);
  border-radius: 8px;
  padding: 10px 14px;
}

.preview-title {
  font-size: 12px;
  font-weight: 600;
  color: var(--el-text-color-secondary);
  margin-bottom: 8px;
}

.preview-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 8px;
  font-size: 13px;
}

.meta-item.full {
  grid-column: span 2;
}

.meta-label {
  color: var(--el-text-color-secondary);
  margin-right: 6px;
}

.meta-val {
  color: var(--el-text-color-primary);
}

.meta-val.code {
  font-family: var(--font-mono, monospace);
  color: var(--el-color-primary);
  font-weight: 500;
}
</style>
