<template>
  <div class="panel-container" style="margin-top: 24px;">
    <div class="panel-header" style="display: flex; justify-content: space-between; align-items: center;">
      <h3>账单导入智能规则</h3>
      <div class="header-actions">
        <el-button size="small" @click="handleResetRules" :loading="rulesLoading">恢复默认规则</el-button>
        <el-button type="primary" size="small" @click="openRuleDialog(null)">新建规则</el-button>
      </div>
    </div>
    <p class="export-hint">
      导入 CSV 账单时，系统将按优先级自动匹配关键词，智能归类到对应分类，减少手工调整。
    </p>

    <el-table :data="importRules" v-loading="rulesLoading" size="small" style="margin-top: 12px;">
      <el-table-column prop="keyword" label="匹配关键词" min-width="120" />
      <el-table-column prop="match_field" label="匹配范围" width="100">
        <template #default="{ row }">
          {{ ({ all: '全部', description: '描述', counterparty: '交易方', note: '备注' } as Record<string, string>)[row.match_field] || row.match_field }}
        </template>
      </el-table-column>
      <el-table-column prop="category_name" label="归入分类" min-width="100">
        <template #default="{ row }">
          <span>{{ row.category_name || '-' }}</span>
        </template>
      </el-table-column>
      <el-table-column prop="target_type" label="类型" width="80">
        <template #default="{ row }">
          <el-tag :type="row.target_type === 'income' ? 'success' : 'warning'" size="small" effect="plain">
            {{ row.target_type === 'income' ? '收入' : '支出' }}
          </el-tag>
        </template>
      </el-table-column>
      <el-table-column prop="priority" label="优先级" width="70" align="center" />
      <el-table-column prop="is_active" label="启用" width="80" align="center">
        <template #default="{ row }">
          <el-switch
            :model-value="!!row.is_active"
            :active-value="true"
            :inactive-value="false"
            size="small"
            @change="toggleRuleActive(row as ImportRule, $event as boolean)"
          />
        </template>
      </el-table-column>
      <el-table-column label="操作" width="100" align="center">
        <template #default="{ row }">
          <el-button link type="primary" size="small" @click="openRuleDialog(row as ImportRule)">编辑</el-button>
          <el-button link type="danger" size="small" @click="handleDeleteRule(row.id)">删除</el-button>
        </template>
      </el-table-column>
    </el-table>

    <el-dialog
      v-model="ruleDialogVisible"
      :title="editingRule?.id ? '编辑导入规则' : '新建导入规则'"
      width="480px"
      append-to-body
    >
      <el-form :model="ruleForm" label-width="90px" size="default">
        <el-form-item label="关键词" required>
          <el-input v-model="ruleForm.keyword" placeholder="例如 美团、滴滴、工资" />
        </el-form-item>
        <el-form-item label="匹配范围">
          <el-select v-model="ruleForm.match_field" style="width: 100%;">
            <el-option label="全部字段" value="all" />
            <el-option label="描述" value="description" />
            <el-option label="交易方" value="counterparty" />
            <el-option label="备注" value="note" />
          </el-select>
        </el-form-item>
        <el-form-item label="目标分类">
          <el-select v-model="ruleForm.category_id" style="width: 100%;" clearable filterable placeholder="选择分类">
            <el-option v-for="cat in flatCategories" :key="cat.id" :label="cat.label" :value="cat.id" />
          </el-select>
        </el-form-item>
        <el-form-item label="交易类型">
          <el-select v-model="ruleForm.target_type" style="width: 100%;">
            <el-option label="支出" value="expense" />
            <el-option label="收入" value="income" />
          </el-select>
        </el-form-item>
        <el-form-item label="优先级">
          <el-input-number v-model="ruleForm.priority" :min="1" :max="999" />
          <span style="margin-left: 8px; color: var(--mf-text-muted); font-size: 12px;">数值越小越优先</span>
        </el-form-item>
        <el-form-item label="启用">
          <el-switch v-model="ruleForm.is_active" />
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="ruleDialogVisible = false">取消</el-button>
        <el-button type="primary" :loading="ruleSaving" @click="saveRule">保存</el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, onMounted, computed } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { importRulesApi } from '@/api/importRules'
import type { ImportRule, ImportRuleCreatePayload } from '@/api/importRules'
import { useCategoryStore } from '@/stores/category'

const categoryStore = useCategoryStore()
const importRules = ref<ImportRule[]>([])
const rulesLoading = ref(false)
const ruleDialogVisible = ref(false)
const ruleSaving = ref(false)
const editingRule = ref<ImportRule | null>(null)

const ruleForm = reactive({
  keyword: '',
  match_field: 'all' as string,
  match_type: 'contains' as string,
  category_id: undefined as number | undefined,
  target_type: 'expense' as string,
  priority: 100,
  is_active: true,
})

const flatCategories = computed(() => {
  const result: { id: number; label: string }[] = []
  const tree = categoryStore.buildTree(categoryStore.allNodes)
  for (const c of tree) {
    result.push({ id: c.id, label: c.name })
    if (c.children) {
      for (const child of c.children) {
        result.push({ id: child.id, label: `${c.name} / ${child.name}` })
      }
    }
  }
  return result
})

async function loadImportRules() {
  rulesLoading.value = true
  try {
    importRules.value = await importRulesApi.list() as unknown as ImportRule[]
  } catch { /* ignore */ } finally {
    rulesLoading.value = false
  }
}

function openRuleDialog(rule: ImportRule | null) {
  editingRule.value = rule
  if (rule) {
    ruleForm.keyword = rule.keyword
    ruleForm.match_field = rule.match_field || 'all'
    ruleForm.match_type = rule.match_type || 'contains'
    ruleForm.category_id = rule.category_id || undefined
    ruleForm.target_type = rule.target_type || 'expense'
    ruleForm.priority = rule.priority || 100
    ruleForm.is_active = !!rule.is_active
  } else {
    ruleForm.keyword = ''
    ruleForm.match_field = 'all'
    ruleForm.match_type = 'contains'
    ruleForm.category_id = undefined
    ruleForm.target_type = 'expense'
    ruleForm.priority = 100
    ruleForm.is_active = true
  }
  ruleDialogVisible.value = true
}

async function toggleRuleActive(row: ImportRule, val: boolean) {
  const payload: ImportRuleCreatePayload = {
    keyword: row.keyword,
    match_field: row.match_field || 'all',
    match_type: row.match_type || 'contains',
    category_id: row.category_id || undefined,
    target_type: row.target_type || 'expense',
    priority: row.priority || 100,
    is_active: val,
  }
  try {
    await importRulesApi.update(row.id, payload)
    row.is_active = val
  } catch {
    ElMessage.error('切换失败')
  }
}

async function saveRule() {
  if (!ruleForm.keyword.trim()) {
    ElMessage.warning('请输入匹配关键词')
    return
  }
  ruleSaving.value = true
  try {
    const payload: ImportRuleCreatePayload = {
      keyword: ruleForm.keyword.trim(),
      match_field: ruleForm.match_field,
      match_type: ruleForm.match_type,
      category_id: ruleForm.category_id,
      target_type: ruleForm.target_type,
      priority: ruleForm.priority,
      is_active: ruleForm.is_active,
    }
    if (editingRule.value?.id) {
      await importRulesApi.update(editingRule.value.id, payload)
      ElMessage.success('规则已更新')
    } else {
      await importRulesApi.create(payload)
      ElMessage.success('规则已创建')
    }
    ruleDialogVisible.value = false
    await loadImportRules()
  } catch { ElMessage.error('保存失败') } finally {
    ruleSaving.value = false
  }
}

async function handleDeleteRule(id: number) {
  try {
    await ElMessageBox.confirm('确定要删除该规则吗？', '确认', { type: 'warning' })
    await importRulesApi.delete(id)
    ElMessage.success('已删除')
    await loadImportRules()
  } catch { /* cancelled */ }
}

async function handleResetRules() {
  try {
    await ElMessageBox.confirm('这将删除所有自定义规则并恢复出厂默认规则，确定继续吗？', '重置确认', { type: 'warning' })
    rulesLoading.value = true
    await importRulesApi.resetDefaults()
    ElMessage.success('已恢复默认规则')
    await loadImportRules()
  } catch { /* cancelled */ } finally {
    rulesLoading.value = false
  }
}

onMounted(() => {
  loadImportRules()
  categoryStore.loadCategories()
})
</script>
