<template>
  <div class="panel-container">
    <div class="panel-header">
      <div class="header-main">
        <h3>{{ t('settings.userMgmtTitle') }}</h3>
        <p class="export-hint">{{ t('settings.userMgmtHint') }}</p>
      </div>
      <div class="header-actions">
        <el-button type="primary" class="action-btn" @click="openCreateDialog">
          <Icon icon="ph:user-plus-bold" width="16" style="margin-right: 6px;" />
          {{ t('settings.userMgmtCreate') }}
        </el-button>
        <el-button :loading="loading" @click="loadUsers">
          <Icon icon="ph:arrows-clockwise" width="16" style="margin-right: 6px;" />
          {{ t('common.refresh') }}
        </el-button>
      </div>
    </div>

    <!-- 用户统计卡片 -->
    <div class="metric-grid" v-loading="loading && users.length === 0">
      <div class="metric-card">
        <div class="metric-icon-box cyan">
          <Icon icon="ph:users-three-bold" width="20" />
        </div>
        <div class="metric-content">
          <div class="metric-label">{{ t('settings.userMgmtTotal') }}</div>
          <div class="metric-val mono">{{ stats.total_users }}</div>
        </div>
      </div>

      <div class="metric-card">
        <div class="metric-icon-box green">
          <Icon icon="ph:check-circle-bold" width="20" />
        </div>
        <div class="metric-content">
          <div class="metric-label">{{ t('settings.userMgmtActive') }}</div>
          <div class="metric-val mono">{{ stats.active_users }}</div>
        </div>
      </div>

      <div class="metric-card">
        <div class="metric-icon-box purple">
          <Icon icon="ph:crown-bold" width="20" />
        </div>
        <div class="metric-content">
          <div class="metric-label">{{ t('settings.userMgmtAdmins') }}</div>
          <div class="metric-val mono">{{ stats.admin_users }}</div>
        </div>
      </div>

      <div class="metric-card">
        <div class="metric-icon-box orange">
          <Icon icon="ph:warning-circle-bold" width="20" />
        </div>
        <div class="metric-content">
          <div class="metric-label">{{ t('settings.userMgmtDisabled') }}</div>
          <div class="metric-val mono">{{ stats.disabled_users }}</div>
        </div>
      </div>
    </div>

    <!-- 搜索与筛选 -->
    <div class="filter-bar">
      <el-input
        v-model="filters.keyword"
        :placeholder="t('settings.userMgmtSearchPlaceholder')"
        clearable
        style="width: 220px;"
        @keyup.enter="handleSearch"
        @clear="handleSearch"
      >
        <template #prefix>
          <Icon icon="ph:magnifying-glass" width="15" />
        </template>
      </el-input>

      <el-select v-model="filters.role" style="width: 140px;" @change="handleSearch">
        <el-option value="all" :label="t('settings.userMgmtAllRoles')" />
        <el-option value="admin" :label="t('settings.userMgmtRoleAdmin')" />
        <el-option value="user" :label="t('settings.userMgmtRoleUser')" />
      </el-select>

      <el-select v-model="filters.status" style="width: 140px;" @change="handleSearch">
        <el-option value="all" :label="t('settings.userMgmtAllStatus')" />
        <el-option value="active" :label="t('settings.userMgmtStatusActive')" />
        <el-option value="disabled" :label="t('settings.userMgmtStatusDisabled')" />
      </el-select>

      <el-button type="primary" plain @click="handleSearch">
        {{ t('common.search') }}
      </el-button>
    </div>

    <!-- 用户表格 -->
    <div class="users-table-wrap">
      <el-table
        :data="users"
        v-loading="loading"
        style="width: 100%;"
        :empty-text="t('settings.userMgmtEmpty')"
        class="users-table"
      >
        <el-table-column prop="id" label="ID" width="76" align="center">
          <template #default="{ row }">
            <span class="mono text-muted">#{{ row.id }}</span>
          </template>
        </el-table-column>

        <el-table-column prop="username" :label="t('settings.username')" min-width="200">
          <template #default="{ row }">
            <div class="user-cell">
              <div class="user-avatar-sm" :class="{ 'is-admin': row.role === 'admin', 'is-disabled': row.disabled }">
                {{ (row.username || 'U').charAt(0).toUpperCase() }}
              </div>
              <div class="user-cell-info">
                <div class="user-name-line">
                  <span class="user-name-text">{{ row.username }}</span>
                  <el-tag v-if="row.id === auth.user?.id" size="small" type="info" effect="plain">
                    {{ t('settings.userMgmtCurrentTag') }}
                  </el-tag>
                  <el-tag v-if="row.oauth_provider" size="small" effect="plain">
                    {{ row.oauth_provider }}
                  </el-tag>
                </div>
              </div>
            </div>
          </template>
        </el-table-column>

        <el-table-column prop="role" :label="t('settings.userMgmtRole')" width="120">
          <template #default="{ row }">
            <el-tag :type="row.role === 'admin' ? 'warning' : 'info'" size="small" effect="light">
              <Icon
                :icon="row.role === 'admin' ? 'ph:crown-bold' : 'ph:user-bold'"
                width="13"
                style="margin-right: 4px; vertical-align: -2px;"
              />
              {{ row.role === 'admin' ? t('settings.userMgmtRoleAdmin') : t('settings.userMgmtRoleUser') }}
            </el-tag>
          </template>
        </el-table-column>

        <el-table-column prop="disabled" :label="t('settings.userMgmtStatus')" width="110">
          <template #default="{ row }">
            <el-tag :type="row.disabled ? 'danger' : 'success'" size="small" effect="light">
              {{ row.disabled ? t('settings.userMgmtStatusDisabled') : t('settings.userMgmtStatusActive') }}
            </el-tag>
          </template>
        </el-table-column>

        <el-table-column prop="totp_enabled" :label="t('settings.tabTwoFactor')" width="120">
          <template #default="{ row }">
            <el-tag :type="row.totp_enabled ? 'success' : 'info'" size="small" effect="plain">
              {{ row.totp_enabled ? t('settings.userMgmt2faOn') : t('settings.userMgmt2faOff') }}
            </el-tag>
          </template>
        </el-table-column>

        <el-table-column prop="created_at" :label="t('settings.registeredAt')" width="175">
          <template #default="{ row }">
            <span class="mono text-muted">{{ formatDateTime(row.created_at) }}</span>
          </template>
        </el-table-column>

        <el-table-column :label="t('common.actions')" min-width="320" align="right">
          <template #default="{ row }">
            <div class="row-actions">
              <el-button
                size="small"
                link
                type="primary"
                :disabled="isSelf(row as AdminUserItem) || isLastActiveAdmin(row as AdminUserItem)"
                @click="handleToggleRole(row as AdminUserItem)"
              >
                {{ row.role === 'admin' ? t('settings.userMgmtSetUser') : t('settings.userMgmtSetAdmin') }}
              </el-button>

              <el-button
                size="small"
                link
                :type="row.disabled ? 'success' : 'warning'"
                :disabled="isSelf(row as AdminUserItem) || isLastActiveAdmin(row as AdminUserItem)"
                @click="handleToggleDisabled(row as AdminUserItem)"
              >
                {{ row.disabled ? t('settings.userMgmtEnable') : t('settings.userMgmtDisable') }}
              </el-button>

              <el-button
                size="small"
                link
                type="primary"
                @click="openResetPwdDialog(row as AdminUserItem)"
              >
                <Icon icon="ph:key-bold" width="13" style="margin-right: 3px;" />
                {{ t('settings.userMgmtResetPwd') }}
              </el-button>

              <el-button
                v-if="row.totp_enabled"
                size="small"
                link
                type="warning"
                @click="handleDisable2Fa(row as AdminUserItem)"
              >
                {{ t('settings.userMgmtDisable2fa') }}
              </el-button>

              <el-button
                size="small"
                link
                type="danger"
                :disabled="isSelf(row as AdminUserItem) || isLastActiveAdmin(row as AdminUserItem)"
                @click="handleDeleteUser(row as AdminUserItem)"
              >
                <Icon icon="ph:trash-bold" width="13" style="margin-right: 3px;" />
                {{ t('common.delete') }}
              </el-button>
            </div>
          </template>
        </el-table-column>
      </el-table>

      <div class="pagination-bar" v-if="total > 0">
        <el-pagination
          v-model:current-page="page"
          v-model:page-size="pageSize"
          :total="total"
          :page-sizes="[10, 20, 50]"
          layout="total, sizes, prev, pager, next"
          background
          @current-change="loadUsers"
          @size-change="handleSizeChange"
        />
      </div>
    </div>

    <!-- 新建用户弹窗 -->
    <el-dialog
      v-model="createVisible"
      :title="t('settings.userMgmtCreateTitle')"
      width="440px"
      destroy-on-close
    >
      <el-form
        ref="createFormRef"
        :model="createForm"
        :rules="createRules"
        label-position="top"
      >
        <el-form-item :label="t('settings.username')" prop="username">
          <el-input
            v-model="createForm.username"
            :placeholder="t('settings.userMgmtUsernamePlaceholder')"
            maxlength="64"
          />
        </el-form-item>

        <el-form-item :label="t('settings.userMgmtRole')" prop="role">
          <el-radio-group v-model="createForm.role">
            <el-radio value="user">{{ t('settings.userMgmtRoleUser') }}</el-radio>
            <el-radio value="admin">{{ t('settings.userMgmtRoleAdmin') }}</el-radio>
          </el-radio-group>
        </el-form-item>

        <el-form-item :label="t('settings.userMgmtInitPwd')" prop="password">
          <el-input
            v-model="createForm.password"
            type="password"
            show-password
            :placeholder="t('settings.passwordMin')"
          />
        </el-form-item>

        <el-form-item :label="t('settings.confirmPassword')" prop="confirmPassword">
          <el-input
            v-model="createForm.confirmPassword"
            type="password"
            show-password
            :placeholder="t('settings.confirmPassword')"
            @keyup.enter="submitCreate"
          />
        </el-form-item>
      </el-form>

      <template #footer>
        <el-button @click="createVisible = false">{{ t('common.cancel') }}</el-button>
        <el-button type="primary" :loading="submitting" @click="submitCreate">
          {{ t('common.confirm') }}
        </el-button>
      </template>
    </el-dialog>

    <!-- 重置密码弹窗 -->
    <el-dialog
      v-model="resetPwdVisible"
      :title="t('settings.userMgmtResetPwdTitle', { name: resetTarget?.username || '' })"
      width="420px"
      destroy-on-close
    >
      <el-form
        ref="resetFormRef"
        :model="resetForm"
        :rules="resetRules"
        label-position="top"
      >
        <el-form-item :label="t('settings.newPassword')" prop="password">
          <el-input
            v-model="resetForm.password"
            type="password"
            show-password
            :placeholder="t('settings.passwordMin')"
          />
        </el-form-item>

        <el-form-item :label="t('settings.confirmPassword')" prop="confirmPassword">
          <el-input
            v-model="resetForm.confirmPassword"
            type="password"
            show-password
            :placeholder="t('settings.confirmPassword')"
            @keyup.enter="submitResetPwd"
          />
        </el-form-item>
      </el-form>

      <template #footer>
        <el-button @click="resetPwdVisible = false">{{ t('common.cancel') }}</el-button>
        <el-button type="primary" :loading="submitting" @click="submitResetPwd">
          {{ t('common.confirm') }}
        </el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, computed, onMounted } from 'vue'
import { ElMessage, ElMessageBox, type FormInstance, type FormRules } from 'element-plus'
import { Icon } from '@iconify/vue'
import { useAuthStore } from '@/stores/auth'
import {
  adminUsersApi,
  type AdminUserItem,
  type AdminUserStats,
  type SystemUserRole,
} from '@/api/admin_users'
import { encryptPassword } from '@/utils/crypto'
import { t } from '@/utils/locale'

const auth = useAuthStore()

const loading = ref(false)
const submitting = ref(false)
const users = ref<AdminUserItem[]>([])
const total = ref(0)
const page = ref(1)
const pageSize = ref(20)

const stats = reactive<AdminUserStats>({
  total_users: 0,
  active_users: 0,
  admin_users: 0,
  active_admin_users: 0,
  disabled_users: 0,
})

const filters = reactive<{
  keyword: string
  role: 'all' | 'admin' | 'user'
  status: 'all' | 'active' | 'disabled'
}>({
  keyword: '',
  role: 'all',
  status: 'all',
})

const activeAdminCount = computed(() => stats.active_admin_users)

function isSelf(row: AdminUserItem): boolean {
  return row.id === auth.user?.id
}

function isLastActiveAdmin(row: AdminUserItem): boolean {
  return row.role === 'admin' && !row.disabled && activeAdminCount.value <= 1
}

function formatDateTime(s: string): string {
  if (!s) return '—'
  return s.replace('T', ' ').slice(0, 19)
}

async function loadUsers() {
  loading.value = true
  try {
    const res = await adminUsersApi.list({
      page: page.value,
      page_size: pageSize.value,
      keyword: filters.keyword.trim() || undefined,
      role: filters.role,
      status: filters.status,
    })
    users.value = res.list || []
    total.value = res.total || 0
    if (res.stats) {
      stats.total_users = res.stats.total_users || 0
      stats.active_users = res.stats.active_users || 0
      stats.admin_users = res.stats.admin_users || 0
      stats.active_admin_users = res.stats.active_admin_users || 0
      stats.disabled_users = res.stats.disabled_users || 0
    }
  } catch {
    // 错误已由 http 拦截器统一提示
  } finally {
    loading.value = false
  }
}

function handleSearch() {
  page.value = 1
  loadUsers()
}

function handleSizeChange() {
  page.value = 1
  loadUsers()
}

// ---------- 新建用户 ----------
const createVisible = ref(false)
const createFormRef = ref<FormInstance>()
const createForm = reactive({
  username: '',
  role: 'user' as SystemUserRole,
  password: '',
  confirmPassword: '',
})

const createRules: FormRules = {
  username: [
    { required: true, message: t('settings.userMgmtUsernamePlaceholder'), trigger: 'blur' },
    { min: 2, max: 64, message: t('settings.userMgmtUsernameRule'), trigger: 'blur' },
  ],
  password: [
    { required: true, message: t('settings.passwordMin'), trigger: 'blur' },
    { min: 6, max: 128, message: t('settings.passwordMin'), trigger: 'blur' },
  ],
  confirmPassword: [
    {
      validator: (_rule, val, cb) => {
        if (val !== createForm.password) cb(new Error(t('settings.passwordMismatch')))
        else cb()
      },
      trigger: 'blur',
    },
  ],
}

function openCreateDialog() {
  createForm.username = ''
  createForm.role = 'user'
  createForm.password = ''
  createForm.confirmPassword = ''
  createVisible.value = true
}

async function submitCreate() {
  if (!createFormRef.value) return
  const valid = await createFormRef.value.validate().catch(() => false)
  if (!valid) return

  submitting.value = true
  try {
    const password_enc = await encryptPassword(createForm.password)
    await adminUsersApi.create({
      username: createForm.username.trim(),
      password_enc,
      role: createForm.role,
    })
    ElMessage.success(t('settings.userMgmtCreateSuccess'))
    createVisible.value = false
    await loadUsers()
  } catch {
    // 拦截器已提示
  } finally {
    submitting.value = false
  }
}

// ---------- 角色与状态变更 ----------
async function handleToggleRole(row: AdminUserItem) {
  const nextRole: SystemUserRole = row.role === 'admin' ? 'user' : 'admin'
  const msg =
    nextRole === 'admin'
      ? t('settings.userMgmtConfirmPromote', { name: row.username })
      : t('settings.userMgmtConfirmDemote', { name: row.username })

  try {
    await ElMessageBox.confirm(msg, t('common.confirm'), { type: 'warning' })
    await adminUsersApi.update(row.id, { role: nextRole })
    ElMessage.success(t('settings.userMgmtUpdateSuccess'))
    await loadUsers()
  } catch {
    // 取消或报错
  }
}

async function handleToggleDisabled(row: AdminUserItem) {
  const nextDisabled = !row.disabled
  const msg = nextDisabled
    ? t('settings.userMgmtConfirmDisable', { name: row.username })
    : t('settings.userMgmtConfirmEnable', { name: row.username })

  try {
    await ElMessageBox.confirm(msg, t('common.confirm'), {
      type: nextDisabled ? 'warning' : 'info',
    })
    await adminUsersApi.update(row.id, { disabled: nextDisabled })
    ElMessage.success(t('settings.userMgmtUpdateSuccess'))
    await loadUsers()
  } catch {
    // 取消或报错
  }
}

// ---------- 重置密码 ----------
const resetPwdVisible = ref(false)
const resetTarget = ref<AdminUserItem | null>(null)
const resetFormRef = ref<FormInstance>()
const resetForm = reactive({
  password: '',
  confirmPassword: '',
})

const resetRules: FormRules = {
  password: [
    { required: true, message: t('settings.passwordMin'), trigger: 'blur' },
    { min: 6, max: 128, message: t('settings.passwordMin'), trigger: 'blur' },
  ],
  confirmPassword: [
    {
      validator: (_rule, val, cb) => {
        if (val !== resetForm.password) cb(new Error(t('settings.passwordMismatch')))
        else cb()
      },
      trigger: 'blur',
    },
  ],
}

function openResetPwdDialog(row: AdminUserItem) {
  resetTarget.value = row
  resetForm.password = ''
  resetForm.confirmPassword = ''
  resetPwdVisible.value = true
}

async function submitResetPwd() {
  if (!resetFormRef.value || !resetTarget.value) return
  const valid = await resetFormRef.value.validate().catch(() => false)
  if (!valid) return

  submitting.value = true
  try {
    const new_password_enc = await encryptPassword(resetForm.password)
    await adminUsersApi.resetPassword(resetTarget.value.id, new_password_enc)
    ElMessage.success(t('settings.userMgmtResetPwdSuccess'))
    resetPwdVisible.value = false
  } catch {
    // 拦截器已提示
  } finally {
    submitting.value = false
  }
}

// ---------- 关闭 2FA ----------
async function handleDisable2Fa(row: AdminUserItem) {
  try {
    await ElMessageBox.confirm(
      t('settings.userMgmtConfirmDisable2fa', { name: row.username }),
      t('common.confirm'),
      { type: 'warning' }
    )
    await adminUsersApi.disable2Fa(row.id)
    ElMessage.success(t('settings.userMgmtDisable2faSuccess'))
    await loadUsers()
  } catch {
    // 取消或报错
  }
}

// ---------- 删除用户 ----------
async function handleDeleteUser(row: AdminUserItem) {
  try {
    await ElMessageBox.confirm(
      t('settings.userMgmtConfirmDelete', { name: row.username }),
      t('common.delete'),
      { type: 'error', confirmButtonText: t('common.delete') }
    )
    await adminUsersApi.remove(row.id)
    ElMessage.success(t('settings.userMgmtDeleteSuccess'))
    await loadUsers()
  } catch {
    // 取消或报错
  }
}

onMounted(() => {
  loadUsers()
})
</script>

<style scoped>
.panel-container {
  /* 与其他设置面板一致：撑满 Settings 主内容区，不额外限宽 */
}

.panel-header {
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
  gap: 16px;
  margin-bottom: 20px;
  flex-wrap: wrap;
}

.header-main h3 {
  margin: 0 0 6px 0;
  font-size: 16px;
  font-weight: 600;
  color: var(--mf-text-main);
}

.export-hint {
  margin: 0;
  font-size: 13px;
  color: var(--mf-text-muted);
}

.header-actions {
  display: flex;
  align-items: center;
  gap: 8px;
}

.metric-grid {
  display: grid;
  grid-template-columns: repeat(4, minmax(0, 1fr));
  gap: 12px;
  margin-bottom: 18px;
}

@media (max-width: 768px) {
  .metric-grid {
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }
}

.metric-card {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 14px 16px;
  border: 1px solid var(--mf-border);
  border-radius: 10px;
  background: var(--mf-surface);
}

.metric-icon-box {
  width: 38px;
  height: 38px;
  border-radius: 10px;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.metric-icon-box.cyan {
  background: rgba(6, 182, 212, 0.14);
  color: #06b6d4;
}

.metric-icon-box.green {
  background: rgba(16, 185, 129, 0.14);
  color: #10b981;
}

.metric-icon-box.purple {
  background: rgba(139, 92, 246, 0.14);
  color: #8b5cf6;
}

.metric-icon-box.orange {
  background: rgba(245, 158, 11, 0.14);
  color: #f59e0b;
}

.metric-label {
  font-size: 12px;
  color: var(--mf-text-muted);
}

.metric-val {
  font-size: 18px;
  font-weight: 700;
  color: var(--mf-text-main);
  margin-top: 2px;
}

.filter-bar {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-bottom: 14px;
  flex-wrap: wrap;
}

.users-table-wrap {
  border: 1px solid var(--mf-border);
  border-radius: 10px;
  padding: 12px;
  background: var(--mf-surface);
}

.user-cell {
  display: flex;
  align-items: center;
  gap: 10px;
}

.user-avatar-sm {
  width: 30px;
  height: 30px;
  border-radius: 8px;
  background: linear-gradient(135deg, #6366f1, #8b5cf6);
  color: #fff;
  font-size: 13px;
  font-weight: 700;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.user-avatar-sm.is-admin {
  background: linear-gradient(135deg, #f59e0b, #ea580c);
}

.user-avatar-sm.is-disabled {
  background: #64748b;
  opacity: 0.7;
}

.user-name-line {
  display: flex;
  align-items: center;
  gap: 6px;
  flex-wrap: wrap;
}

.user-name-text {
  font-weight: 600;
  color: var(--mf-text-main);
}

.row-actions {
  display: flex;
  align-items: center;
  justify-content: flex-end;
  gap: 4px;
  flex-wrap: wrap;
}

.pagination-bar {
  display: flex;
  justify-content: flex-end;
  margin-top: 14px;
}

.mono {
  font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace;
}

.text-muted {
  color: var(--mf-text-muted);
  font-size: 12px;
}
</style>
