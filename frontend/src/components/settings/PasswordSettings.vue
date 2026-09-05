<template>
  <div class="panel-container">
    <div class="panel-header">
      <h3>{{ t('settings.changePassword') }}</h3>
    </div>
    <el-form
      :model="form"
      :rules="rules"
      ref="formRef"
      label-width="120px"
      class="premium-form"
      @keyup.enter="submit"
    >
      <el-form-item :label="t('settings.oldPassword')" prop="old_password">
        <el-input
          v-model="form.old_password"
          type="password"
          show-password
          :placeholder="t('settings.oldPassword')"
        />
      </el-form-item>
      <el-form-item :label="t('settings.newPassword')" prop="new_password">
        <el-input
          v-model="form.new_password"
          type="password"
          show-password
          :placeholder="t('settings.newPassword')"
        />
      </el-form-item>
      <el-form-item :label="t('settings.confirmPassword')" prop="confirmPassword">
        <el-input
          v-model="form.confirmPassword"
          type="password"
          show-password
          :placeholder="t('settings.confirmPassword')"
        />
      </el-form-item>
      <el-form-item>
        <el-button
          type="primary"
          class="action-btn"
          @click="submit"
          :loading="loading"
        >
          {{ t('settings.savePassword') }}
        </el-button>
      </el-form-item>
    </el-form>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive } from 'vue'
import { ElMessage } from 'element-plus'
import { useAuthStore } from '@/stores/auth'
import { t } from '@/utils/locale'
import { encryptText } from '@/utils/crypto'
import type { FormInstance, FormRules } from 'element-plus'

const auth = useAuthStore()
const loading = ref(false)
const formRef = ref<FormInstance>()

const form = reactive({
  old_password: '',
  new_password: '',
  confirmPassword: '',
})

const validateConfirm = (rule: any, value: string, callback: any) => {
  if (value !== form.new_password) {
    callback(new Error(t('settings.passwordMismatch')))
  } else {
    callback()
  }
}

const rules: FormRules = {
  old_password: [
    { required: true, message: t('settings.oldPassword'), trigger: 'blur' },
  ],
  new_password: [
    { required: true, message: t('settings.newPassword'), trigger: 'blur' },
    { min: 6, message: t('settings.passwordMin'), trigger: 'blur' },
  ],
  confirmPassword: [
    { required: true, message: t('settings.confirmPassword'), trigger: 'blur' },
    { validator: validateConfirm, trigger: 'blur' },
  ],
}

async function submit() {
  if (!formRef.value) return
  await formRef.value.validate(async (valid: boolean) => {
    if (!valid) return
    try {
      loading.value = true
      const old_enc = await encryptText(form.old_password)
      const new_enc = await encryptText(form.new_password)
      await auth.changePassword(old_enc, new_enc)
      ElMessage.success(t('settings.passwordSuccess'))
      form.old_password = ''
      form.new_password = ''
      form.confirmPassword = ''
    } catch {
      // error handled by http interceptor
    } finally {
      loading.value = false
    }
  })
}
</script>
