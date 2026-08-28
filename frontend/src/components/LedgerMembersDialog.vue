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
    :title="`成员与权限管理 - ${ledger?.name || ''}`"
    width="640px"
    destroy-on-close
    append-to-body
    class="premium-dialog"
    @update:model-value="emit('update:visible', $event)"
  >
    <div class="members-container" v-loading="loading">
      <!-- Invite Code Section -->
      <div class="invite-banner">
        <div class="invite-info">
          <div class="title">
            <Icon icon="ph:share-network" width="18" />
            <span>邀请码快速加入</span>
          </div>
          <div class="desc">生成 6 位专属加入码，家庭成员输入即可快速加入并协同记账</div>
        </div>
        <div class="invite-actions">
          <template v-if="inviteData">
            <el-tag size="large" type="success" class="invite-tag">
              {{ inviteData.invite_code }}
            </el-tag>
            <el-button size="small" type="primary" plain @click="copyInviteCode">
              复制
            </el-button>
          </template>
          <el-button
            v-else
            size="small"
            type="primary"
            :loading="generatingInvite"
            @click="handleGenerateInvite"
          >
            生成邀请码
          </el-button>
        </div>
      </div>

      <!-- Add by Username -->
      <div class="add-member-bar">
        <el-input
          v-model="newUsername"
          placeholder="输入系统已有成员的用户名"
          style="width: 260px"
          clearable
        />
        <el-select v-model="newRole" style="width: 130px">
          <el-option label="记账者 (可写)" value="editor" />
          <el-option label="只读 (仅查账)" value="viewer" />
        </el-select>
        <el-button type="primary" :loading="addingMember" @click="handleAddMember">
          添加成员
        </el-button>
      </div>

      <!-- Members Table -->
      <el-table :data="members" border stripe style="width: 100%; margin-top: 16px">
        <el-table-column prop="username" label="用户名" min-width="120">
          <template #default="{ row }">
            <div class="user-cell">
              <Icon icon="ph:user-circle" width="20" class="user-icon" />
              <span class="user-name">{{ row.username }}</span>
            </div>
          </template>
        </el-table-column>

        <el-table-column prop="role" label="角色与权限" width="160">
          <template #default="{ row }">
            <el-tag v-if="row.role === 'owner'" type="danger" effect="dark" size="small">
              所有者 (Owner)
            </el-tag>
            <el-select
              v-else
              :model-value="row.role"
              size="small"
              style="width: 130px"
              @change="(val: 'editor' | 'viewer') => handleUpdateRole(row as LedgerMember, val)"
            >
              <el-option label="记账者 (Editor)" value="editor" />
              <el-option label="只读查账 (Viewer)" value="viewer" />
            </el-select>
          </template>
        </el-table-column>

        <el-table-column prop="joined_at" label="加入时间" width="160" />

        <el-table-column label="操作" width="80" align="center">
          <template #default="{ row }">
            <el-button
              v-if="row.role !== 'owner'"
              type="danger"
              link
              size="small"
              @click="handleRemoveMember(row as LedgerMember)"
            >
              移出
            </el-button>
            <span v-else class="text-muted">-</span>
          </template>
        </el-table-column>
      </el-table>
    </div>

    <template #footer>
      <el-button @click="emit('update:visible', false)">关闭</el-button>
    </template>
  </el-dialog>
</template>

<style scoped>
.members-container {
  display: flex;
  flex-direction: column;
}
.invite-banner {
  background: var(--el-color-primary-light-9);
  border: 1px solid var(--el-color-primary-light-7);
  border-radius: 8px;
  padding: 12px 16px;
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 16px;
}
.invite-info .title {
  display: flex;
  align-items: center;
  gap: 6px;
  font-weight: 600;
  color: var(--el-color-primary-dark-2);
  margin-bottom: 4px;
}
.invite-info .desc {
  font-size: 12px;
  color: var(--el-text-color-secondary);
}
.invite-actions {
  display: flex;
  align-items: center;
  gap: 8px;
}
.invite-tag {
  font-family: monospace;
  font-size: 15px;
  font-weight: 700;
  letter-spacing: 2px;
}
.add-member-bar {
  display: flex;
  gap: 10px;
  align-items: center;
}
.user-cell {
  display: flex;
  align-items: center;
  gap: 8px;
}
.user-icon {
  color: var(--el-color-primary);
}
.user-name {
  font-weight: 500;
}
.text-muted {
  color: var(--el-text-color-placeholder);
}
</style>
