<script setup lang="ts">
import { ref } from 'vue'
import { ElMessage } from 'element-plus'
import { ledgerApi } from '@/api/ledgers'

const props = defineProps<{
  visible: boolean
}>()

const emit = defineEmits<{
  (e: 'update:visible', val: boolean): void
  (e: 'joined', id: number): void
}>()

const inviteCode = ref('')
const submitting = ref(false)

async function handleJoin() {
  const code = inviteCode.value.trim().toUpperCase()
  if (!code) {
    ElMessage.warning('请输入 6 位邀请码')
    return
  }
  submitting.value = true
  try {
    const res = await ledgerApi.joinByInvite(code)
    ElMessage.success(`成功加入账本「${res.name}」`)
    inviteCode.value = ''
    emit('joined', res.id)
    emit('update:visible', false)
  } finally {
    submitting.value = false
  }
}
</script>

<template>
  <el-dialog
    :model-value="visible"
    width="440px"
    destroy-on-close
    append-to-body
    class="premium-dialog"
    @update:model-value="emit('update:visible', $event)"
  >
    <template #header>
      <div class="modal-header">
        <div class="modal-header-icon join">
          <Icon icon="ph:key-bold" width="22" />
        </div>
        <div class="modal-header-text">
          <div class="modal-title">加入协同账本</div>
          <div class="modal-subtitle">输入所有者提供的 6 位专属邀请码</div>
        </div>
      </div>
    </template>

    <div class="join-body">
      <div class="tip-banner">
        <Icon icon="ph:info-bold" width="16" />
        <span>加入后您将与账本所有者共享流水与资产核算空间</span>
      </div>
      <div class="input-wrap">
        <el-input
          v-model="inviteCode"
          placeholder="输入 6 位邀请码 (如: J79CZU)"
          maxlength="12"
          size="large"
          class="code-input"
          clearable
          @keyup.enter="handleJoin"
        >
          <template #prefix>
            <Icon icon="ph:ticket" width="18" class="ticket-icon" />
          </template>
        </el-input>
      </div>
    </div>

    <template #footer>
      <div class="dialog-footer">
        <el-button @click="emit('update:visible', false)">取消</el-button>
        <el-button
          type="primary"
          class="submit-btn"
          :loading="submitting"
          @click="handleJoin"
        >
          <template #icon>
            <Icon icon="ph:check-bold" width="15" />
          </template>
          立即加入
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
.modal-header-icon.join {
  width: 44px;
  height: 44px;
  border-radius: 12px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: var(--mf-primary-light);
  border: 1px solid var(--mf-primary-border);
  color: var(--mf-primary);
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.2);
  flex-shrink: 0;
}
.modal-header-text {
  display: flex;
  flex-direction: column;
  gap: 3px;
}
.modal-title {
  font-size: 18px;
  font-weight: 700;
  color: var(--mf-text-main);
}
.modal-subtitle {
  font-size: 12px;
  color: var(--mf-text-regular);
}
.join-body {
  display: flex;
  flex-direction: column;
  gap: 16px;
  padding: 6px 0;
}
.tip-banner {
  display: flex;
  align-items: center;
  gap: 8px;
  background: var(--mf-primary-light);
  border: 1px solid var(--mf-primary-border);
  border-radius: 8px;
  padding: 10px 14px;
  font-size: 12.5px;
  color: var(--mf-text-regular);
}
.code-input :deep(.el-input__wrapper) {
  padding: 6px 14px;
  border-radius: 10px;
}
.code-input :deep(input) {
  font-family: monospace;
  font-size: 20px;
  font-weight: 700;
  letter-spacing: 3px;
  text-transform: uppercase;
  color: var(--mf-primary);
  text-align: center;
}
.ticket-icon {
  color: var(--mf-primary);
}
.dialog-footer {
  display: flex;
  justify-content: flex-end;
  gap: 10px;
}
.submit-btn {
  border-radius: 8px;
  font-weight: 600;
  padding: 8px 20px;
}
</style>
