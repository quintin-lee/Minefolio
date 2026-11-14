<template>
  <el-drawer
    v-model="visible"
    :title="isEdit ? `编辑技能: ${form.name}` : '新建 AI 专属技能'"
    size="680px"
    destroy-on-close
    :close-on-click-modal="false"
    class="skill-drawer"
  >
    <el-tabs v-model="activeTab" class="skill-tabs">
      <!-- Tab 1: L1 Metadata -->
      <el-tab-pane label="基础元数据" name="l1">
        <el-form
          ref="formRef"
          :model="form"
          :rules="rules"
          label-position="top"
          class="meta-form"
        >
          <el-form-item label="技能唯一标识 (name)" prop="name">
            <el-input
              v-model="form.name"
              placeholder="例如: crypto-arbitrage-scanner (kebab-case)"
              :disabled="isEdit"
              clearable
            >
              <template #prefix>
                <Icon icon="ph:terminal" class="input-icon" />
              </template>
            </el-input>
            <div class="field-hint">
              由小写字母、数字与连字符构成（如 <code>financial-health-auditor</code>），保存后不可修改。
            </div>
          </el-form-item>

          <el-form-item label="功能描述 (description)" prop="description">
            <el-input
              v-model="form.description"
              type="textarea"
              :rows="2"
              placeholder="清晰阐明此技能解决的财务场景与触发条件，供 AI 决策调用..."
              maxlength="300"
              show-word-limit
            />
          </el-form-item>

          <div class="form-row-2">
            <el-form-item label="业务分类 (category)" prop="category">
              <el-select v-model="form.category" style="width: 100%">
                <el-option label="财务审计 (finance)" value="finance" />
                <el-option label="投资再平衡 (investment)" value="investment" />
                <el-option label="支出风控 (expense)" value="expense" />
                <el-option label="税务分类 (tax)" value="tax" />
                <el-option label="风险管理 (risk)" value="risk" />
                <el-option label="其他工具 (other)" value="other" />
              </el-select>
            </el-form-item>

            <el-form-item label="开源许可协议 (license)" prop="license">
              <el-input v-model="form.license" placeholder="MIT, Apache-2.0..." />
            </el-form-item>
          </div>

          <div class="form-row-2">
            <el-form-item label="系统兼容性 (compatibility)" prop="compatibility">
              <el-input v-model="form.compatibility" placeholder="minefolio >= 1.0" />
            </el-form-item>

            <el-form-item label="技能标签 (tags, 逗号分隔)">
              <el-input v-model="form.tags" placeholder="portfolio, rebalance, tax..." />
            </el-form-item>
          </div>

          <el-form-item label="启用状态">
            <el-switch
              v-model="formEnabled"
              active-text="启用"
              inactive-text="禁用"
            />
          </el-form-item>
        </el-form>
      </el-tab-pane>

      <!-- Tab 2: L2 Prompt Instructions -->
      <el-tab-pane label="规则指令提示词" name="l2">
        <div class="l2-container">
          <div class="editor-header">
            <div class="editor-title">
              <Icon icon="ph:file-text-duotone" />
              <span>Markdown 指令规则 (L2 Instructions)</span>
            </div>
            <div class="token-counter">
              <span>{{ (form.instructions_l2 || '').length }} 字符</span>
              <span class="divider">/</span>
              <span>约 {{ tokenEstimate }} Tokens</span>
            </div>
          </div>

          <el-input
            v-model="form.instructions_l2"
            type="textarea"
            :rows="16"
            placeholder="在此书写该技能激活时注入 AI 模型的专业领域指南、分析框架与输出规范 (Markdown 格式)..."
            class="markdown-editor"
          />

          <el-alert
            type="info"
            :closable="false"
            show-icon
            class="l2-tip"
          >
            <template #title>
              <span>编写专业 Skill 的提示</span>
            </template>
            明确列出该领域分析的核对指标、公式步骤（如现金流倍数、最大回撤测算）以及返回格式，模型将在使用 <code>use_skill</code> 时自动阅读本段指令。
          </el-alert>
        </div>
      </el-tab-pane>

      <!-- Tab 3: L3 Execution Config -->
      <el-tab-pane label="执行配置" name="l3">
        <div class="l3-container">
          <el-alert
            type="warning"
            :closable="false"
            show-icon
            class="l3-security-alert"
          >
            <template #title>
              <span>安全规范：本地沙箱进程执行 (L3 Process)</span>
            </template>
            仅实例管理员有权配置本地脚本进程执行路径。纯提示词型技能无需填写此页，留空即可按标准 L2 指令执行。
          </el-alert>

          <el-form label-position="top" class="l3-form">
            <el-form-item label="执行脚本路径 (exec_path)">
              <el-input
                v-model="form.exec_path"
                placeholder="例如: scripts/tax_calc.py 或 bin/tax_tool (相对路径)"
                clearable
              />
              <div class="field-hint">相对于工作根目录的相对路径（禁止以 / 开头或包含 .. 穿越）。</div>
            </el-form-item>

            <el-form-item label="工作根目录 (exec_root)">
              <el-input
                v-model="form.exec_root"
                placeholder="例如: /var/lib/minefolio/skills/tax"
                clearable
              />
            </el-form-item>

            <div class="form-row-2">
              <el-form-item label="默认附加参数 (exec_args)">
                <el-input
                  v-model="form.exec_args"
                  placeholder="--format=json --strict"
                />
              </el-form-item>

              <el-form-item label="环境变量 (exec_env, KEY=VAL 换行)">
                <el-input
                  v-model="form.exec_env"
                  placeholder="ENV=production"
                />
              </el-form-item>
            </div>

            <div class="form-row-2">
              <el-form-item label="超时时间 (timeout_ms)">
                <el-input-number
                  v-model="form.timeout_ms"
                  :min="500"
                  :max="60000"
                  :step="1000"
                  style="width: 100%"
                />
              </el-form-item>

              <el-form-item label="最大输出缓冲 (max_output_bytes)">
                <el-input-number
                  v-model="form.max_output_bytes"
                  :min="1024"
                  :max="1048576"
                  :step="4096"
                  style="width: 100%"
                />
              </el-form-item>
            </div>
          </el-form>
        </div>
      </el-tab-pane>

      <!-- Tab 4: Interactive Test Runner -->
      <el-tab-pane v-if="isEdit" label="实时调试" name="test">
        <div class="test-container">
          <div class="test-header">
            <span class="test-title">调试运行技能 (Test Runner)</span>
            <el-button
              type="primary"
              :loading="testing"
              @click="handleRunTest"
            >
              <Icon icon="ph:play-fill" class="btn-icon" />
              运行测试
            </el-button>
          </div>

          <el-form label-position="top">
            <el-form-item label="测试入参 (arguments, JSON 格式)">
              <el-input
                v-model="testArgs"
                type="textarea"
                :rows="3"
                placeholder='{"portfolio_id": 1, "test_param": "hello"}'
              />
            </el-form-item>
          </el-form>

          <div v-if="testResult" class="test-result-box">
            <div class="result-title-bar">
              <div class="result-badge-wrap">
                <el-tag :type="testResult.status === 'success' ? 'success' : 'danger'">
                  {{ testResult.status === 'success' ? '执行成功' : '执行失败' }}
                </el-tag>
                <el-tag type="info" effect="plain">
                  {{ testResult.mode === 'l3' ? 'L3 本地进程' : 'L2 指令预检' }}
                </el-tag>
                <span v-if="testResult.char_count" class="result-metric">
                  {{ testResult.char_count }} 字符
                </span>
                <span v-if="testResult.token_estimate" class="result-metric">
                  约 {{ testResult.token_estimate }} Tokens
                </span>
              </div>
            </div>

            <div class="result-content">
              <pre class="code-pre">{{ formattedTestOutput }}</pre>
            </div>
          </div>
        </div>
      </el-tab-pane>
    </el-tabs>

    <template #footer>
      <div class="drawer-footer">
        <el-button @click="visible = false">取消</el-button>
        <el-button type="primary" :loading="saving" @click="handleSave">
          保存技能
        </el-button>
      </div>
    </template>
  </el-drawer>
</template>

<script setup lang="ts">
import { ref, reactive, computed } from 'vue'
import { ElMessage } from 'element-plus'
import type { FormInstance, FormRules } from 'element-plus'
import { Icon } from '@iconify/vue'
import type { AiSkill, SkillCreatePayload, SkillUpdatePayload, SkillTestResult } from '@/types'
import { createSkill, updateSkill, testSkill } from '@/api/skills'

const emit = defineEmits<{
  (e: 'saved'): void
}>()

const visible = ref(false)
const isEdit = ref(false)
const currentSkillId = ref<number>(0)
const activeTab = ref('l1')
const saving = ref(false)
const testing = ref(false)
const testArgs = ref('')
const testResult = ref<SkillTestResult | null>(null)

const formRef = ref<FormInstance>()

const form = reactive({
  name: '',
  description: '',
  license: 'MIT',
  compatibility: 'minefolio >= 1.0',
  category: 'finance',
  tags: '',
  enabled: 1,
  instructions_l2: '',
  exec_path: '',
  exec_root: '',
  exec_args: '',
  exec_env: '',
  timeout_ms: 5000,
  max_output_bytes: 65536,
})

const formEnabled = computed({
  get: () => form.enabled === 1,
  set: (val: boolean) => {
    form.enabled = val ? 1 : 0
  },
})

const tokenEstimate = computed(() => {
  const len = (form.instructions_l2 || '').length
  return Math.ceil(len / 4)
})

const formattedTestOutput = computed(() => {
  if (!testResult.value) return ''
  if (testResult.value.exec_result) {
    return JSON.stringify(testResult.value.exec_result, null, 2)
  }
  if (typeof testResult.value.output === 'string' && testResult.value.output.length > 0) {
    return testResult.value.output
  }
  if (testResult.value.mode === 'l2') {
    const preview = testResult.value.instructions ?? testResult.value.instructions_preview ?? ''
    return preview || '(当前技能尚未填写 L2 指令规则正文)'
  }
  return JSON.stringify(testResult.value, null, 2)
})

const rules: FormRules = {
  name: [
    { required: true, message: '技能名称为必填项', trigger: 'blur' },
    {
      pattern: /^[a-z0-9]+(-[a-z0-9]+)*$/,
      message: '技能名称必须为合法 kebab-case（小写字母、数字及连字符，如 financial-auditor）',
      trigger: 'blur',
    },
  ],
  description: [
    { required: true, message: '技能描述为必填项', trigger: 'blur' },
  ],
  category: [
    { required: true, message: '请选择技能分类', trigger: 'change' },
  ],
}

function openCreate() {
  isEdit.value = false
  currentSkillId.value = 0
  activeTab.value = 'l1'
  testResult.value = null
  testArgs.value = ''

  form.name = ''
  form.description = ''
  form.license = 'MIT'
  form.compatibility = 'minefolio >= 1.0'
  form.category = 'finance'
  form.tags = ''
  form.enabled = 1
  form.instructions_l2 = ''
  form.exec_path = ''
  form.exec_root = ''
  form.exec_args = ''
  form.exec_env = ''
  form.timeout_ms = 5000
  form.max_output_bytes = 65536

  visible.value = true
}

function openEdit(skill: AiSkill, tab: string = 'l1') {
  isEdit.value = true
  currentSkillId.value = skill.id
  activeTab.value = tab
  testResult.value = null
  testArgs.value = ''

  form.name = skill.name
  form.description = skill.description
  form.license = skill.license || 'MIT'
  form.compatibility = skill.compatibility || 'minefolio >= 1.0'
  form.category = skill.category || 'finance'
  form.tags = skill.tags || ''
  form.enabled = skill.enabled
  form.instructions_l2 = skill.instructions_l2 || ''
  form.exec_path = skill.exec_path || ''
  form.exec_root = skill.exec_root || ''
  form.exec_args = skill.exec_args || ''
  form.exec_env = skill.exec_env || ''
  form.timeout_ms = skill.timeout_ms || 5000
  form.max_output_bytes = skill.max_output_bytes || 65536

  visible.value = true
}

async function handleSave() {
  if (!formRef.value) return
  await formRef.value.validate(async (valid) => {
    if (!valid) {
      activeTab.value = 'l1'
      return
    }

    try {
      saving.value = true
      if (isEdit.value) {
        const payload: SkillUpdatePayload = {
          description: form.description,
          license: form.license,
          compatibility: form.compatibility,
          category: form.category,
          tags: form.tags,
          enabled: form.enabled,
          instructions_l2: form.instructions_l2,
          exec_path: form.exec_path,
          exec_root: form.exec_root,
          exec_args: form.exec_args,
          exec_env: form.exec_env,
          timeout_ms: form.timeout_ms,
          max_output_bytes: form.max_output_bytes,
        }
        await updateSkill(currentSkillId.value, payload)
        ElMessage.success('技能更新成功')
      } else {
        const payload: SkillCreatePayload = {
          name: form.name,
          description: form.description,
          license: form.license,
          compatibility: form.compatibility,
          category: form.category,
          tags: form.tags,
          enabled: form.enabled,
          instructions_l2: form.instructions_l2,
          exec_path: form.exec_path,
          exec_root: form.exec_root,
          exec_args: form.exec_args,
          exec_env: form.exec_env,
          timeout_ms: form.timeout_ms,
          max_output_bytes: form.max_output_bytes,
        }
        await createSkill(payload)
        ElMessage.success('技能创建成功')
      }
      visible.value = false
      emit('saved')
    } catch (err: unknown) {
      const e = err as { response?: { data?: { message?: string } }; message?: string }
      ElMessage.error(e?.response?.data?.message || e?.message || '保存失败')
    } finally {
      saving.value = false
    }
  })
}

async function handleRunTest() {
  if (!currentSkillId.value) return
  try {
    testing.value = true
    testResult.value = await testSkill(currentSkillId.value, testArgs.value)
    ElMessage.success('测试执行完成')
  } catch (err: unknown) {
    const e = err as { response?: { data?: { message?: string } }; message?: string }
    ElMessage.error(e?.response?.data?.message || e?.message || '测试执行失败')
  } finally {
    testing.value = false
  }
}

defineExpose({
  openCreate,
  openEdit,
})
</script>

<style scoped>
.skill-tabs {
  margin-top: -10px;
}

.meta-form,
.l3-form {
  padding-top: 8px;
}

.form-row-2 {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 16px;
}

.field-hint {
  font-size: 12px;
  color: var(--el-text-color-secondary);
  line-height: 1.4;
  margin-top: 4px;
}

.input-icon {
  font-size: 15px;
  color: var(--el-text-color-secondary);
}

.l2-container {
  display: flex;
  flex-direction: column;
  gap: 12px;
  padding-top: 8px;
}

.editor-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.editor-title {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 14px;
  font-weight: 500;
  color: var(--el-text-color-primary);
}

.token-counter {
  font-size: 12px;
  color: var(--el-text-color-secondary);
  font-family: var(--font-mono, monospace);
}

.token-counter .divider {
  margin: 0 4px;
  opacity: 0.5;
}

.markdown-editor :deep(.el-textarea__inner) {
  font-family: var(--font-mono, monospace);
  font-size: 13px;
  line-height: 1.6;
  border-radius: 8px;
}

.l2-tip,
.l3-security-alert {
  margin-top: 8px;
  border-radius: 8px;
}

.test-container {
  display: flex;
  flex-direction: column;
  gap: 16px;
  padding-top: 8px;
}

.test-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.test-title {
  font-size: 14px;
  font-weight: 600;
  color: var(--el-text-color-primary);
}

.btn-icon {
  margin-right: 4px;
}

.test-result-box {
  background: var(--el-fill-color-light);
  border: 1px solid var(--el-border-color-lighter);
  border-radius: 8px;
  padding: 12px;
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.result-title-bar {
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.result-badge-wrap {
  display: flex;
  align-items: center;
  gap: 8px;
}

.result-metric {
  font-size: 12px;
  color: var(--el-text-color-secondary);
}

.result-content {
  background: var(--el-bg-color-overlay);
  border-radius: 6px;
  padding: 10px;
  max-height: 280px;
  overflow-y: auto;
}

.code-pre {
  margin: 0;
  font-family: var(--font-mono, monospace);
  font-size: 12px;
  line-height: 1.5;
  color: var(--el-text-color-primary);
  white-space: pre-wrap;
  word-break: break-all;
}

.drawer-footer {
  display: flex;
  align-items: center;
  justify-content: flex-end;
  gap: 12px;
}
</style>
