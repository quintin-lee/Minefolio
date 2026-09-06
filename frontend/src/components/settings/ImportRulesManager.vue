<template>
  <div class="panel-container" style="margin-top: 24px;">
    <div class="panel-header" style="display: flex; justify-content: space-between; align-items: center;">
      <h3>{{ t('settings.importRulesTitle') }}</h3>
      <div class="header-actions">
        <el-button size="small" @click="handleResetRules" :loading="rulesLoading">{{ t('settings.resetDefaultRules') }}</el-button>
        <el-button type="primary" size="small" @click="openRuleDialog(null)">{{ t('settings.newRule') }}</el-button>
      </div>
    </div>
    <p class="export-hint">
      {{ t('settings.importRulesHint') }}
    </p>

    <el-table :data="importRules" v-loading="rulesLoading" size="small" style="margin-top: 12px;">
      <el-table-column prop="keyword" :label="t('settings.ruleKeywordCol')" min-width="120" />
      <el-table-column prop="match_field" :label="t('settings.ruleMatchField')" width="100">
        <template #default="{ row }">
          {{ matchFieldLabel(row.match_field) }}
        </template>
      </el-table-column>
      <el-table-column prop="category_name" :label="t('settings.ruleCategory')" min-width="100">
        <template #default="{ row }">
          <span>{{ row.category_name || '-' }}</span>
        </template>
      </el-table-column>
      <el-table-column prop="target_type" :label="t('settings.ruleType')" width="80">
        <template #default="{ row }">
          <el-tag :type="row.target_type === 'income' ? 'success' : 'warning'" size="small" effect="plain">
            {{ row.target_type === 'income' ? t('dailyExpenses.income') : t('dailyExpenses.expense') }}
          </el-tag>
        </template>
      </el-table-column>
      <el-table-column prop="priority" :label="t('settings.rulePriority')" width="70" align="center" />
      <el-table-column prop="is_active" :label="t('settings.ruleEnabled')" width="80" align="center">
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
      <el-table-column :label="t('common.action')" width="100" align="center">
        <template #default="{ row }">
          <el-button link type="primary" size="small" @click="openRuleDialog(row as ImportRule)">{{ t('common.edit') }}</el-button>
          <el-button link type="danger" size="small" @click="handleDeleteRule(row.id)">{{ t('common.delete') }}</el-button>
        </template>
      </el-table-column>
    </el-table>

    <el-dialog
      v-model="ruleDialogVisible"
      :title="editingRule?.id ? t('settings.ruleEditDialog') : t('settings.ruleNewDialog')"
      width="480px"
      append-to-body
    >
      <el-form :model="ruleForm" label-width="90px" size="default">
        <el-form-item :label="t('settings.ruleKeywordField')" required>
          <el-input v-model="ruleForm.keyword" :placeholder="t('settings.ruleKeywordPlaceholder')" />
        </el-form-item>
        <el-form-item :label="t('settings.ruleMatchField')">
          <el-select v-model="ruleForm.match_field" style="width: 100%;">
            <el-option :label="t('settings.matchFieldAll')" value="all" />
            <el-option :label="t('settings.matchFieldDesc')" value="description" />
            <el-option :label="t('settings.matchFieldCounterparty')" value="counterparty" />
            <el-option :label="t('settings.matchFieldNote')" value="note" />
          </el-select>
        </el-form-item>
        <el-form-item :label="t('settings.ruleCategory')">
          <el-select v-model="ruleForm.category_id" style="width: 100%;" clearable filterable :placeholder="t('categories.selectCategory')">
            <el-option v-for="cat in flatCategories" :key="cat.id" :label="cat.label" :value="cat.id" />
          </el-select>
        </el-form-item>
        <el-form-item :label="t('transactions.transactionType')">
          <el-select v-model="ruleForm.target_type" style="width: 100%;">
            <el-option :label="t('dailyExpenses.expense')" value="expense" />
            <el-option :label="t('dailyExpenses.income')" value="income" />
          </el-select>
        </el-form-item>
        <el-form-item :label="t('settings.rulePriority')">
          <el-input-number v-model="ruleForm.priority" :min="1" :max="999" />
          <span style="margin-left: 8px; color: var(--mf-text-muted); font-size: 12px;">{{ t('settings.priorityHint') }}</span>
        </el-form-item>
        <el-form-item :label="t('settings.ruleEnabled')">
          <el-switch v-model="ruleForm.is_active" />
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="ruleDialogVisible = false">{{ t('common.cancel') }}</el-button>
        <el-button type="primary" :loading="ruleSaving" @click="saveRule">{{ t('common.save') }}</el-button>
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
import { t } from '@/utils/locale'

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

function matchFieldLabel(field: string): string {
  const map: Record<string, string> = {
    all: t('settings.matchAllShort'),
    description: t('settings.matchFieldDesc'),
    counterparty: t('settings.matchFieldCounterparty'),
    note: t('settings.matchFieldNote'),
  }
  return map[field] || field
}

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
    ElMessage.error(t('settings.ruleToggleFailed'))
  }
}

async function saveRule() {
  if (!ruleForm.keyword.trim()) {
    ElMessage.warning(t('settings.ruleKeywordRequired'))
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
      ElMessage.success(t('settings.ruleUpdated'))
    } else {
      await importRulesApi.create(payload)
      ElMessage.success(t('settings.ruleCreated'))
    }
    ruleDialogVisible.value = false
    await loadImportRules()
  } catch { ElMessage.error(t('settings.saveFailed')) } finally {
    ruleSaving.value = false
  }
}

async function handleDeleteRule(id: number) {
  try {
    await ElMessageBox.confirm(t('settings.ruleDeleteConfirm'), t('common.confirm'), { type: 'warning' })
    await importRulesApi.delete(id)
    ElMessage.success(t('settings.ruleDeleted'))
    await loadImportRules()
  } catch { /* cancelled */ }
}

async function handleResetRules() {
  try {
    await ElMessageBox.confirm(t('settings.resetConfirm'), t('settings.resetConfirmTitle'), { type: 'warning' })
    rulesLoading.value = true
    await importRulesApi.resetDefaults()
    ElMessage.success(t('settings.rulesRestored'))
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
