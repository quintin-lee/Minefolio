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
    title="加入家庭 / 协同账本"
    width="420px"
    destroy-on-close
    @update:model-value="emit('update:visible', $event)"
  >
    <div class="join-body">
      <div class="tip">请输入账本所有者分享给您的 6 位邀请码：</div>
      <el-input
        v-model="inviteCode"
        placeholder="例如：MF8392"
        maxlength="10"
        size="large"
        class="code-input"
        clearable
        @keyup.enter="handleJoin"
      />
    </div>

    <template #footer>
      <el-button @click="emit('update:visible', false)">取消</el-button>
      <el-button type="primary" :loading="submitting" @click="handleJoin">
        立即加入
      </el-button>
    </template>
  </el-dialog>
</template>

<style scoped>
.join-body {
  padding: 10px 0;
}
.tip {
  font-size: 13px;
  color: var(--el-text-color-secondary);
  margin-bottom: 12px;
}
.code-input {
  font-family: monospace;
  font-size: 18px;
  font-weight: 700;
  letter-spacing: 2px;
  text-transform: uppercase;
}
</style>
