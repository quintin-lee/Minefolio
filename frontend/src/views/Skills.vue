<template>
  <div class="skills-page">
    <!-- Header Banner -->
    <div class="page-header">
      <div class="header-intro">
        <h1 class="page-title">
          <Icon icon="ph:magic-wand-duotone" class="title-icon" />
          Agent Skills 技能库
        </h1>
        <p class="page-subtitle">
          基于 Anthropic Agent Skills 规范的领域专属技能。模型在对话分析时通过 <code>use_skill</code> 自动激活并遵循专业准则。
        </p>
      </div>

      <div class="header-actions">
        <el-button
          plain
          :loading="resetting"
          @click="handleResetDefaults"
        >
          <Icon icon="ph:arrow-counter-clockwise-duotone" class="btn-icon" />
          重置预置技能
        </el-button>

        <el-button
          plain
          @click="openImport"
        >
          <Icon icon="ph:file-arrow-up-duotone" class="btn-icon" />
          导入 SKILL.md
        </el-button>

        <el-button
          type="primary"
          @click="openCreate"
        >
          <Icon icon="ph:plus-bold" class="btn-icon" />
          新建技能
        </el-button>
      </div>
    </div>

    <!-- Filter & Stats Toolbar -->
    <div class="toolbar-card">
      <div class="search-filter-wrap">
        <el-input
          v-model="searchQuery"
          placeholder="搜索技能名称、描述或标签..."
          clearable
          class="search-input"
        >
          <template #prefix>
            <Icon icon="ph:magnifying-glass" class="search-icon" />
          </template>
        </el-input>

        <el-radio-group v-model="selectedCategory" size="default" class="category-filter">
          <el-radio-button label="all">全部</el-radio-button>
          <el-radio-button label="finance">财务审计</el-radio-button>
          <el-radio-button label="investment">投资再平衡</el-radio-button>
          <el-radio-button label="expense">支出风控</el-radio-button>
          <el-radio-button label="tax">税务分类</el-radio-button>
          <el-radio-button label="risk">风险管理</el-radio-button>
        </el-radio-group>
      </div>

      <div class="toggle-stats-wrap">
        <el-checkbox v-model="enabledOnly">
          仅显示已启用
        </el-checkbox>

        <div class="stats-pills">
          <span class="pill-item">
            共 <b>{{ skills.length }}</b> 个技能
          </span>
          <span class="pill-divider">·</span>
          <span class="pill-item success">
            已启用 <b>{{ enabledCount }}</b>
          </span>
          <span class="pill-divider">·</span>
          <span class="pill-item warning">
            本地脚本 <b>{{ l3Count }}</b>
          </span>
        </div>
      </div>
    </div>

    <!-- Skills Gallery -->
    <div v-loading="loading" class="skills-grid-container">
      <div v-if="filteredSkills.length > 0" class="skills-grid">
        <SkillCard
          v-for="skill in filteredSkills"
          :key="skill.id"
          :skill="skill"
          @edit="handleEdit"
          @delete="handleDelete"
          @toggle="handleToggle"
          @test="handleTest"
          @export="handleExport"
        />
      </div>

      <el-empty
        v-else-if="!loading"
        description="未检索到匹配的 AI 专属技能"
        class="empty-state"
      >
        <template #extra>
          <el-button type="primary" plain @click="openCreate">新建第一个技能</el-button>
          <el-button plain @click="handleResetDefaults">恢复预置金融技能</el-button>
        </template>
      </el-empty>
    </div>

    <!-- Dialogs -->
    <SkillDialog ref="dialogRef" @saved="loadSkills" />
    <SkillImportDialog ref="importDialogRef" @imported="loadSkills" />
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { Icon } from '@iconify/vue'
import type { AiSkill } from '@/types'
import { listSkills, toggleSkill, deleteSkill, exportSkillMarkdown, resetDefaultSkills } from '@/api/skills'
import SkillCard from '@/components/skills/SkillCard.vue'
import SkillDialog from '@/components/skills/SkillDialog.vue'
import SkillImportDialog from '@/components/skills/SkillImportDialog.vue'

const loading = ref(false)
const resetting = ref(false)
const skills = ref<AiSkill[]>([])

const searchQuery = ref('')
const selectedCategory = ref('all')
const enabledOnly = ref(false)

const dialogRef = ref<InstanceType<typeof SkillDialog>>()
const importDialogRef = ref<InstanceType<typeof SkillImportDialog>>()

const enabledCount = computed(() => {
  return skills.value.filter(s => s.enabled === 1).length
})

const l3Count = computed(() => {
  return skills.value.filter(s => !!(s.exec_path && s.exec_path.trim().length > 0)).length
})

const filteredSkills = computed(() => {
  return skills.value.filter(s => {
    if (enabledOnly.value && s.enabled !== 1) {
      return false
    }
    if (selectedCategory.value !== 'all' && s.category !== selectedCategory.value) {
      return false
    }
    if (searchQuery.value.trim()) {
      const q = searchQuery.value.trim().toLowerCase()
      const matchName = s.name.toLowerCase().includes(q)
      const matchDesc = (s.description || '').toLowerCase().includes(q)
      const matchTags = (s.tags || '').toLowerCase().includes(q)
      if (!matchName && !matchDesc && !matchTags) {
        return false
      }
    }
    return true
  })
})

async function loadSkills() {
  try {
    loading.value = true
    skills.value = await listSkills()
  } catch (err: unknown) {
    const e = err as { response?: { data?: { message?: string } }; message?: string }
    ElMessage.error(e?.response?.data?.message || e?.message || '加载技能列表失败')
  } finally {
    loading.value = false
  }
}

function openCreate() {
  dialogRef.value?.openCreate()
}

function openImport() {
  importDialogRef.value?.open()
}

function handleEdit(skill: AiSkill) {
  dialogRef.value?.openEdit(skill)
}

function handleTest(skill: AiSkill) {
  dialogRef.value?.openEdit(skill)
}

async function handleToggle(skill: AiSkill, enabled: boolean) {
  try {
    await toggleSkill(skill.id, enabled)
    skill.enabled = enabled ? 1 : 0
    ElMessage.success(enabled ? `技能 ${skill.name} 已启用` : `技能 ${skill.name} 已禁用`)
  } catch (err: unknown) {
    const e = err as { response?: { data?: { message?: string } }; message?: string }
    ElMessage.error(e?.response?.data?.message || e?.message || '切换状态失败')
    await loadSkills()
  }
}

async function handleDelete(skill: AiSkill) {
  try {
    await ElMessageBox.confirm(
      `确定要删除技能「${skill.name}」吗？删除后 AI 将无法调用该技能。`,
      '确认删除',
      {
        confirmButtonText: '删除',
        cancelButtonText: '取消',
        type: 'warning',
      }
    )
    await deleteSkill(skill.id)
    ElMessage.success('技能已成功删除')
    await loadSkills()
  } catch {
    // User cancelled
  }
}

async function handleExport(skill: AiSkill) {
  try {
    const content = await exportSkillMarkdown(skill.id)
    if (!content) {
      ElMessage.warning('导出的内容为空')
      return
    }

    const blob = new Blob([content], { type: 'text/markdown;charset=utf-8' })
    const url = URL.createObjectURL(blob)
    const a = document.createElement('a')
    a.href = url
    a.download = `${skill.name}.SKILL.md`
    document.body.appendChild(a)
    a.click()
    document.body.removeChild(a)
    URL.revokeObjectURL(url)
    ElMessage.success(`已导出 ${skill.name}.SKILL.md`)
  } catch (err: unknown) {
    const e = err as { response?: { data?: { message?: string } }; message?: string }
    ElMessage.error(e?.response?.data?.message || e?.message || '导出失败')
  }
}

async function handleResetDefaults() {
  try {
    await ElMessageBox.confirm(
      '重置预置技能将恢复系统内置的 4 大金融专业技能（财务审计、再平衡建议、支出风控、税务凭据）。确定重置？',
      '重置预置技能',
      {
        confirmButtonText: '确认重置',
        cancelButtonText: '取消',
        type: 'info',
      }
    )
    resetting.value = true
    await resetDefaultSkills()
    ElMessage.success('已恢复内置预置金融技能')
    await loadSkills()
  } catch {
    // Cancelled
  } finally {
    resetting.value = false
  }
}

onMounted(() => {
  loadSkills()
})
</script>

<style scoped>
.skills-page {
  display: flex;
  flex-direction: column;
  gap: 20px;
  padding: 4px 0 24px 0;
}

.page-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  flex-wrap: wrap;
  gap: 16px;
}

.header-intro {
  max-width: 720px;
}

.page-title {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 22px;
  font-weight: 700;
  color: var(--el-text-color-primary);
  margin: 0 0 6px 0;
}

.title-icon {
  font-size: 26px;
  color: var(--el-color-primary);
}

.page-subtitle {
  font-size: 13px;
  color: var(--el-text-color-secondary);
  line-height: 1.5;
  margin: 0;
}

.page-subtitle code {
  background: var(--el-fill-color);
  padding: 2px 6px;
  border-radius: 4px;
  color: var(--el-color-primary);
  font-family: var(--font-mono, monospace);
}

.header-actions {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-wrap: wrap;
}

.btn-icon {
  margin-right: 4px;
  font-size: 15px;
}

.toolbar-card {
  background: var(--el-bg-color);
  border: 1px solid var(--el-border-color-lighter);
  border-radius: 12px;
  padding: 14px 18px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  flex-wrap: wrap;
  gap: 14px;
}

.search-filter-wrap {
  display: flex;
  align-items: center;
  gap: 12px;
  flex-wrap: wrap;
  flex: 1;
}

.search-input {
  width: 260px;
}

.search-icon {
  font-size: 16px;
  color: var(--el-text-color-placeholder);
}

.category-filter {
  flex-wrap: wrap;
}

.toggle-stats-wrap {
  display: flex;
  align-items: center;
  gap: 18px;
  flex-wrap: wrap;
}

.stats-pills {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 13px;
  color: var(--el-text-color-regular);
}

.pill-divider {
  color: var(--el-text-color-placeholder);
}

.pill-item b {
  font-family: var(--font-mono, monospace);
  color: var(--el-text-color-primary);
}

.pill-item.success b {
  color: #10b981;
}

.pill-item.warning b {
  color: #f59e0b;
}

.skills-grid-container {
  min-height: 320px;
}

.skills-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(320px, 1fr));
  gap: 16px;
}

.empty-state {
  padding: 60px 0;
  background: var(--el-bg-color);
  border: 1px dashed var(--el-border-color);
  border-radius: 12px;
}
</style>
