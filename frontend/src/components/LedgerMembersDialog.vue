<script setup lang="ts">
import { ref, watch } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { Icon } from '@iconify/vue'
import { ledgerApi } from '@/api/ledgers'
import type { Ledger, LedgerMember, LedgerInviteResult } from '@/types'

const props = defineProps<{
  visible: boolean
  ledger: Ledger | null
}>()

const emit = defineEmits<{
  (e: 'update:visible', val: boolean): void
  (e: 'refresh'): void
}>()

const members = ref<LedgerMember[]>([])
const loading = ref(false)
const inviteData = ref<LedgerInviteResult | null>(null)
const generatingInvite = ref(false)

// Add member form
const newUsername = ref('')
const newRole = ref<'editor' | 'viewer'>('editor')
const addingMember = ref(false)

watch(
  () => props.visible,
  (val) => {
    if (val && props.ledger) {
      loadMembers()
      inviteData.value = null
      newUsername.value = ''
      newRole.value = 'editor'
    }
  }
)

async function loadMembers() {
  if (!props.ledger) return
  loading.value = true
  try {
    const data = await ledgerApi.listMembers(props.ledger.id)
    members.value = Array.isArray(data) ? data : []
  } finally {
    loading.value = false
  }
}

async function handleAddMember() {
  if (!props.ledger || !newUsername.value.trim()) {
    ElMessage.warning('请输入要添加的用户名')
    return
  }
  addingMember.value = true
  try {
    await ledgerApi.addMember(props.ledger.id, newUsername.value.trim(), newRole.value)
    ElMessage.success(`成功添加成员 ${newUsername.value}`)
    newUsername.value = ''
    loadMembers()
    emit('refresh')
  } finally {
    addingMember.value = false
  }
}

async function handleGenerateInvite() {
  if (!props.ledger) return
  generatingInvite.value = true
  try {
    inviteData.value = await ledgerApi.createInviteCode(props.ledger.id)
    ElMessage.success('已生成 7 天有效期的专属邀请码')
  } finally {
    generatingInvite.value = false
  }
}

async function copyInviteCode() {
  if (!inviteData.value?.invite_code) return
  await navigator.clipboard.writeText(inviteData.value.invite_code)
  ElMessage.success('邀请码已复制到剪贴板')
}

async function handleUpdateRole(member: LedgerMember, role: 'editor' | 'viewer') {
  if (!props.ledger) return
  try {
    await ledgerApi.updateMember(props.ledger.id, member.user_id, role)
    ElMessage.success('成员权限已更新')
    loadMembers()
  } catch (err) {
    // revert or handle
  }
}

async function handleRemoveMember(member: LedgerMember) {
  if (!props.ledger) return
  const isSelf = member.role === 'owner' // cannot remove owner
  if (isSelf) return

  await ElMessageBox.confirm(
    `确定要将成员「${member.username}」移出当前账本吗？`,
    '移出成员',
    {
      confirmButtonText: '确定',
      cancelButtonText: '取消',
      type: 'warning'
    }
  )

  try {
    await ledgerApi.removeMember(props.ledger.id, member.user_id)
    ElMessage.success('已移出成员')
    loadMembers()
    emit('refresh')
  } catch (err) {
    // handled
  }
}
</script>

<template>
  <el-dialog
    :model-value="visible"
    width="680px"
    destroy-on-close
    append-to-body
    class="premium-dialog ledger-members-modal"
    :show-close="true"
    @update:model-value="emit('update:visible', $event)"
  >
    <template #header>
      <div class="modal-header">
        <div
          class="modal-header-icon"
          :style="{
            backgroundColor: ledger?.color ? ledger.color + '20' : 'rgba(0, 212, 255, 0.15)',
            borderColor: ledger?.color ? ledger.color + '50' : 'rgba(0, 212, 255, 0.3)',
            color: ledger?.color || 'var(--mf-primary)'
          }"
        >
          <Icon :icon="ledger?.icon || 'ph:users-three-bold'" width="22" />
        </div>
        <div class="modal-header-text">
          <div class="modal-title">
            <span>成员与权限管理</span>
            <el-tag
              v-if="ledger?.name"
              size="small"
              effect="plain"
              class="ledger-badge"
              :style="{ borderColor: ledger?.color || 'var(--mf-primary)', color: ledger?.color || 'var(--mf-primary)' }"
            >
              {{ ledger.name }}
            </el-tag>
          </div>
          <div class="modal-subtitle">
            协同管理家庭与空间账本成员，分配记账者或只读查账权限
          </div>
        </div>
      </div>
    </template>

    <div class="members-container" v-loading="loading">
      <!-- 1. Invite Code Section -->
      <div class="invite-banner">
        <div class="invite-left">
          <div class="invite-icon-box">
            <Icon icon="ph:share-network-bold" width="22" />
          </div>
          <div class="invite-info">
            <div class="invite-title">邀请码快速加入</div>
            <div class="invite-desc">生成 6 位专属邀请码（7天有效），家庭成员输入即可一键加入协同</div>
          </div>
        </div>
        <div class="invite-right">
          <template v-if="inviteData">
            <div class="code-pill">
              <span class="invite-code-text">{{ inviteData.invite_code }}</span>
              <el-tooltip content="点击复制邀请码" placement="top">
                <button class="copy-btn" @click="copyInviteCode">
                  <Icon icon="ph:copy-bold" width="16" />
                </button>
              </el-tooltip>
            </div>
          </template>
          <el-button
            v-else
            size="default"
            type="primary"
            class="glow-button"
            :loading="generatingInvite"
            @click="handleGenerateInvite"
          >
            <template #icon>
              <Icon icon="ph:key-bold" width="16" />
            </template>
            生成邀请码
          </el-button>
        </div>
      </div>

      <!-- 2. Add Member Bar -->
      <div class="add-member-section">
        <div class="section-title">
          <Icon icon="ph:user-plus-bold" width="16" />
          <span>添加已有成员</span>
        </div>
        <div class="add-member-bar">
          <el-input
            v-model="newUsername"
            placeholder="输入成员用户名 (如: bob)"
            class="user-input"
            clearable
            @keyup.enter="handleAddMember"
          >
            <template #prefix>
              <Icon icon="ph:user" width="16" class="input-icon" />
            </template>
          </el-input>

          <el-select v-model="newRole" class="role-select">
            <el-option label="记账者 (可编辑/记账)" value="editor">
              <div class="role-option">
                <Icon icon="ph:pencil-simple-line-bold" width="16" class="role-opt-icon editor" />
                <span>记账者 (Editor)</span>
              </div>
            </el-option>
            <el-option label="只读 (仅查账)" value="viewer">
              <div class="role-option">
                <Icon icon="ph:eye-bold" width="16" class="role-opt-icon viewer" />
                <span>只读 (Viewer)</span>
              </div>
            </el-option>
          </el-select>

          <el-button
            type="primary"
            class="add-btn"
            :loading="addingMember"
            @click="handleAddMember"
          >
            <template #icon>
              <Icon icon="ph:plus-bold" width="15" />
            </template>
            添加成员
          </el-button>
        </div>
      </div>

      <!-- 3. Members List Table -->
      <div class="members-table-wrap">
        <div class="table-header-meta">
          <div class="section-title">
            <Icon icon="ph:users-three-bold" width="16" />
            <span>已加入成员 ({{ members.length }})</span>
          </div>
        </div>

        <el-table
          :data="members"
          class="custom-members-table"
          :header-cell-style="{ background: 'var(--mf-surface)', color: 'var(--mf-text-regular)', fontWeight: '600' }"
        >
          <el-table-column label="成员" min-width="150">
            <template #default="{ row }">
              <div class="member-cell">
                <div class="avatar-circle">
                  {{ (row.username || 'U').charAt(0).toUpperCase() }}
                </div>
                <div class="member-info">
                  <span class="member-username">{{ row.username }}</span>
                </div>
              </div>
            </template>
          </el-table-column>

          <el-table-column label="权限角色" width="180">
            <template #default="{ row }">
              <div v-if="row.role === 'owner'" class="role-badge owner">
                <Icon icon="ph:crown-bold" width="15" />
                <span>所有者 (Owner)</span>
              </div>
              <el-select
                v-else
                :model-value="row.role"
                size="small"
                class="member-role-select"
                @change="(val: 'editor' | 'viewer') => handleUpdateRole(row as LedgerMember, val)"
              >
                <el-option label="记账者 (可写)" value="editor" />
                <el-option label="只读 (仅查账)" value="viewer" />
              </el-select>
            </template>
          </el-table-column>

          <el-table-column label="加入时间" width="160">
            <template #default="{ row }">
              <div class="time-cell">
                <Icon icon="ph:clock" width="14" />
                <span>{{ row.joined_at?.split(' ')[0] || row.joined_at }}</span>
              </div>
            </template>
          </el-table-column>

          <el-table-column label="操作" width="80" align="center">
            <template #default="{ row }">
              <el-tooltip
                v-if="row.role !== 'owner'"
                content="移出此账本"
                placement="top"
              >
                <el-button
                  type="danger"
                  link
                  size="small"
                  class="remove-btn"
                  @click="handleRemoveMember(row as LedgerMember)"
                >
                  <Icon icon="ph:trash-bold" width="16" />
                </el-button>
              </el-tooltip>
              <span v-else class="owner-lock-hint">
                <Icon icon="ph:lock-key-bold" width="14" />
              </span>
            </template>
          </el-table-column>
        </el-table>
      </div>
    </div>

    <template #footer>
      <div class="dialog-footer">
        <el-button @click="emit('update:visible', false)">
          关闭
        </el-button>
      </div>
    </template>
  </el-dialog>
</template>

<style scoped>
.modal-header {
  display: flex;
  align-items: center;
  gap: 14px;
  padding: 4px 0;
}
.modal-header-icon {
  width: 44px;
  height: 44px;
  border-radius: 12px;
  display: flex;
  align-items: center;
  justify-content: center;
  border: 1px solid;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.2);
  flex-shrink: 0;
}
.modal-header-text {
  display: flex;
  flex-direction: column;
  gap: 4px;
}
.modal-title {
  display: flex;
  align-items: center;
  gap: 10px;
  font-size: 18px;
  font-weight: 700;
  color: var(--mf-text-main);
  letter-spacing: 0.5px;
}
.ledger-badge {
  border-radius: 6px;
  font-weight: 600;
  background: transparent;
}
.modal-subtitle {
  font-size: 12px;
  color: var(--mf-text-regular);
}

.members-container {
  display: flex;
  flex-direction: column;
  gap: 20px;
}

/* 1. Invite Banner */
.invite-banner {
  background: linear-gradient(135deg, var(--mf-primary-light) 0%, rgba(99, 102, 241, 0.08) 100%);
  border: 1px solid var(--mf-primary-border);
  border-radius: var(--mf-radius-lg);
  padding: 16px 20px;
  display: flex;
  justify-content: space-between;
  align-items: center;
  box-shadow: var(--mf-shadow-sm);
}
.invite-left {
  display: flex;
  align-items: center;
  gap: 14px;
}
.invite-icon-box {
  width: 40px;
  height: 40px;
  border-radius: 10px;
  background: var(--mf-primary-light);
  border: 1px solid var(--mf-primary-border);
  color: var(--mf-primary);
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}
.invite-title {
  font-size: 15px;
  font-weight: 600;
  color: var(--mf-text-main);
  margin-bottom: 3px;
}
.invite-desc {
  font-size: 12px;
  color: var(--mf-text-regular);
}
.code-pill {
  display: flex;
  align-items: center;
  background: var(--mf-surface-muted);
  border: 1px solid var(--mf-primary-border);
  border-radius: 10px;
  padding: 4px 6px 4px 14px;
  gap: 10px;
  box-shadow: var(--mf-shadow-sm);
}
.invite-code-text {
  font-family: monospace;
  font-size: 18px;
  font-weight: 700;
  letter-spacing: 3px;
  color: var(--mf-primary);
  text-shadow: none;
}
.copy-btn {
  background: var(--mf-primary-light);
  border: 1px solid var(--mf-primary-border);
  color: var(--mf-primary);
  width: 32px;
  height: 32px;
  border-radius: 8px;
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  transition: all 0.2s;
}
.copy-btn:hover {
  background: var(--mf-primary);
  box-shadow: var(--mf-shadow-glow);
  color: #ffffff;
}
.glow-button {
  box-shadow: 0 0 12px rgba(0, 212, 255, 0.25);
  border-radius: 8px;
}

/* 2. Add Member Section */
.add-member-section {
  display: flex;
  flex-direction: column;
  gap: 10px;
}
.section-title {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 13px;
  font-weight: 600;
  color: var(--mf-text-regular);
}
.add-member-bar {
  display: flex;
  gap: 12px;
  align-items: center;
}
.user-input {
  flex: 1;
}
.role-select {
  width: 170px;
}
.role-option {
  display: flex;
  align-items: center;
  gap: 8px;
}
.role-opt-icon.editor {
  color: #10b981;
}
.role-opt-icon.viewer {
  color: #60a5fa;
}
.add-btn {
  border-radius: 8px;
  font-weight: 600;
  padding: 8px 18px;
}

/* 3. Table */
.members-table-wrap {
  display: flex;
  flex-direction: column;
  gap: 10px;
}
.table-header-meta {
  display: flex;
  justify-content: space-between;
  align-items: center;
}
.custom-members-table {
  border-radius: var(--mf-radius-md);
  overflow: hidden;
  border: 1px solid var(--mf-border);
}
.member-cell {
  display: flex;
  align-items: center;
  gap: 10px;
}
.avatar-circle {
  width: 32px;
  height: 32px;
  border-radius: 50%;
  background: linear-gradient(135deg, var(--mf-primary) 0%, var(--mf-accent) 100%);
  color: white;
  display: flex;
  align-items: center;
  justify-content: center;
  font-weight: 700;
  font-size: 14px;
  box-shadow: var(--mf-shadow-sm);
  flex-shrink: 0;
}
.member-username {
  font-weight: 600;
  color: var(--mf-text-main);
  font-size: 13.5px;
}
.role-badge.owner {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 4px 10px;
  border-radius: 6px;
  background: rgba(245, 158, 11, 0.12);
  border: 1px solid rgba(245, 158, 11, 0.35);
  color: #f59e0b;
  font-size: 12px;
  font-weight: 600;
}
.member-role-select {
  width: 140px;
}
.time-cell {
  display: flex;
  align-items: center;
  gap: 6px;
  color: var(--mf-text-regular);
  font-size: 12px;
}
.remove-btn {
  font-size: 16px;
  transition: transform 0.2s;
}
.remove-btn:hover {
  transform: scale(1.15);
}
.owner-lock-hint {
  color: var(--mf-text-muted);
  display: flex;
  align-items: center;
  justify-content: center;
}
.dialog-footer {
  display: flex;
  justify-content: flex-end;
}
</style>
