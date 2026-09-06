<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { Icon } from '@iconify/vue'
import { ElMessageBox, ElMessage } from 'element-plus'
import { useLedgerStore } from '@/stores/ledger'
import { ledgerApi } from '@/api/ledgers'
import type { Ledger } from '@/types'
import LedgerDialog from './LedgerDialog.vue'
import LedgerMembersDialog from './LedgerMembersDialog.vue'
import JoinLedgerDialog from './JoinLedgerDialog.vue'

const ledgerStore = useLedgerStore()

const ledgerDialogVisible = ref(false)
const editingLedger = ref<Ledger | null>(null)
const membersDialogVisible = ref(false)
const selectedLedger = ref<Ledger | null>(null)
const joinDialogVisible = ref(false)

onMounted(async () => {
  try {
    await ledgerStore.fetchLedgers()
  } catch (e) {
    // silent catch
  }
})

function handleSelectLedger(id: number) {
  ledgerStore.setCurrentLedger(id)
}

function openCreateLedger() {
  editingLedger.value = null
  ledgerDialogVisible.value = true
}

function openEditLedger(ledger: Ledger) {
  editingLedger.value = ledger
  ledgerDialogVisible.value = true
}

function openMembersDialog(ledger: Ledger) {
  selectedLedger.value = ledger
  membersDialogVisible.value = true
}

function openJoinDialog() {
  joinDialogVisible.value = true
}

async function handleLedgerSaved(id?: number) {
  await ledgerStore.fetchLedgers()
  if (id) ledgerStore.setCurrentLedger(id)
}

async function handleJoined(id: number) {
  await ledgerStore.fetchLedgers()
  ledgerStore.setCurrentLedger(id)
}

async function handleDeleteLedger(ledger: Ledger) {
  if (ledger.is_default && ledgerStore.ledgers.length <= 1) {
    ElMessage.warning('默认主账本不可删除')
    return
  }

  await ElMessageBox.confirm(
    `确定要解散并删除账本「${ledger.name}」吗？账本内的私有资产与流水记录将被一并清理！`,
    '解散账本',
    {
      confirmButtonText: '确定删除',
      cancelButtonText: '取消',
      type: 'error'
    }
  )

  try {
    await ledgerApi.delete(ledger.id)
    ElMessage.success('账本已解散')
    await ledgerStore.fetchLedgers()
  } catch (err) {
    // handled
  }
}
</script>

<template>
  <div class="ledger-selector-wrap">
    <el-dropdown trigger="click" placement="bottom-start" class="ledger-dropdown">
      <div class="current-ledger-btn">
        <div
          class="ledger-icon-badge"
          :style="{ backgroundColor: ledgerStore.currentLedger?.color || '#3b82f6' }"
        >
          <Icon :icon="ledgerStore.currentLedger?.icon || 'ph:wallet'" width="16" color="#fff" />
        </div>
        <span class="ledger-name">{{ ledgerStore.currentLedger?.name || '默认账本' }}</span>
        <el-tag
          v-if="ledgerStore.currentLedger"
          size="small"
          :type="
            ledgerStore.currentLedger.my_role === 'owner'
              ? 'primary'
              : ledgerStore.currentLedger.my_role === 'editor'
                ? 'success'
                : 'info'
          "
          class="role-tag"
        >
          {{
            ledgerStore.currentLedger.my_role === 'owner'
              ? '所有者'
              : ledgerStore.currentLedger.my_role === 'editor'
                ? '记账'
                : '只读'
          }}
        </el-tag>
        <Icon icon="ph:caret-down-bold" width="12" class="caret" />
      </div>

      <template #dropdown>
        <el-dropdown-menu class="ledger-menu">
          <div class="menu-header">切换空间与账本</div>
          <div class="ledger-list-scroll">
            <div
              v-for="l in ledgerStore.ledgers"
              :key="l.id"
              class="ledger-item"
              :class="{ active: l.id === ledgerStore.currentLedger?.id }"
              @click="handleSelectLedger(l.id)"
            >
              <div class="item-left">
                <div class="item-icon" :style="{ backgroundColor: l.color || '#3b82f6' }">
                  <Icon :icon="l.icon || 'ph:wallet'" width="16" color="#fff" />
                </div>
                <div class="item-meta">
                  <div class="item-name">
                    <span>{{ l.name }}</span>
                    <el-tag size="small" effect="plain" class="item-role">
                      {{ l.my_role === 'owner' ? '所有者' : l.my_role === 'editor' ? '记账' : '只读' }}
                    </el-tag>
                  </div>
                  <div class="item-sub">
                    <span v-if="l.member_count && l.member_count > 1">
                      <Icon icon="ph:users" width="12" /> {{ l.member_count }}人
                    </span>
                    <span v-if="l.currency">币种: {{ l.currency }}</span>
                  </div>
                </div>
              </div>

              <div class="item-actions" @click.stop>
                <el-tooltip content="成员与权限" placement="top">
                  <el-button
                    type="primary"
                    link
                    size="small"
                    @click="openMembersDialog(l)"
                  >
                    <Icon icon="ph:users" width="16" />
                  </el-button>
                </el-tooltip>
                <el-tooltip v-if="l.my_role === 'owner'" content="编辑账本" placement="top">
                  <el-button
                    type="primary"
                    link
                    size="small"
                    @click="openEditLedger(l)"
                  >
                    <Icon icon="ph:pencil-simple" width="16" />
                  </el-button>
                </el-tooltip>
                <el-tooltip
                  v-if="l.my_role === 'owner' && !l.is_default"
                  content="解散账本"
                  placement="top"
                >
                  <el-button
                    type="danger"
                    link
                    size="small"
                    @click="handleDeleteLedger(l)"
                  >
                    <Icon icon="ph:trash" width="16" />
                  </el-button>
                </el-tooltip>
              </div>
            </div>
          </div>

          <div class="menu-divider" />

          <div class="menu-footer">
            <el-button type="primary" link size="small" @click="openCreateLedger">
              <Icon icon="ph:plus-circle" width="16" />
              <span>新建账本</span>
            </el-button>
            <el-button type="primary" link size="small" @click="openJoinDialog">
              <Icon icon="ph:key" width="16" />
              <span>输入邀请码</span>
            </el-button>
          </div>
        </el-dropdown-menu>
      </template>
    </el-dropdown>

    <!-- Dialogs -->
    <LedgerDialog
      v-model:visible="ledgerDialogVisible"
      :ledger="editingLedger"
      @saved="handleLedgerSaved"
    />
    <LedgerMembersDialog
      v-model:visible="membersDialogVisible"
      :ledger="selectedLedger"
      @refresh="ledgerStore.fetchLedgers"
    />
    <JoinLedgerDialog
      v-model:visible="joinDialogVisible"
      @joined="handleJoined"
    />
  </div>
</template>

<style scoped>
.ledger-selector-wrap {
  display: inline-flex;
  align-items: center;
  margin-right: 12px;
}
.ledger-dropdown :deep(.el-tooltip__trigger:focus-visible) {
  outline: none;
}
.current-ledger-btn {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 6px 12px;
  background: rgba(255, 255, 255, 0.03);
  border: 1px solid transparent;
  border-radius: 20px;
  cursor: pointer;
  transition: all 0.2s ease;
  user-select: none;
  outline: none;
}
.current-ledger-btn:hover {
  background: rgba(0, 212, 255, 0.06);
  border-color: rgba(0, 212, 255, 0.15);
}
.current-ledger-btn:focus,
.current-ledger-btn:focus-visible {
  outline: none;
}
.ledger-icon-badge {
  width: 24px;
  height: 24px;
  border-radius: 6px;
  display: flex;
  align-items: center;
  justify-content: center;
  box-shadow: var(--mf-shadow-sm);
}
.ledger-name {
  font-size: 14px;
  font-weight: 600;
  color: #e2e8f0;
  max-width: 140px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.role-tag {
  font-size: 11px;
  padding: 0 6px;
  height: 20px;
  line-height: 18px;
  border-radius: 4px;
}
.caret {
  color: #94a3b8;
}

.ledger-menu {
  width: 320px;
  padding: 8px;
}
.menu-header {
  font-size: 12px;
  font-weight: 600;
  color: var(--el-text-color-secondary);
  padding: 4px 8px 8px;
}
.ledger-list-scroll {
  max-height: 280px;
  overflow-y: auto;
}
.ledger-item {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 8px;
  border-radius: 6px;
  cursor: pointer;
  transition: background 0.2s;
}
.ledger-item:hover {
  background: var(--el-fill-color-light);
}
.ledger-item.active {
  background: var(--el-color-primary-light-9);
}
.item-left {
  display: flex;
  align-items: center;
  gap: 8px;
  flex: 1;
  min-width: 0;
}
.item-icon {
  width: 28px;
  height: 28px;
  border-radius: 6px;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}
.item-meta {
  flex: 1;
  min-width: 0;
}
.item-name {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 13px;
  font-weight: 600;
  color: var(--el-text-color-primary);
}
.item-name span {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.item-role {
  font-size: 10px;
  padding: 0 4px;
  height: 18px;
  line-height: 16px;
}
.item-sub {
  font-size: 11px;
  color: var(--el-text-color-secondary);
  display: flex;
  gap: 8px;
  margin-top: 2px;
}
.item-actions {
  display: flex;
  align-items: center;
  gap: 2px;
}
.menu-divider {
  height: 1px;
  background: var(--el-border-color-lighter);
  margin: 8px 0 6px;
}
.menu-footer {
  display: flex;
  justify-content: space-between;
  padding: 0 4px;
}
</style>
