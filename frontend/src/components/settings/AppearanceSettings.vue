<template>
  <div class="panel-container">
    <div class="panel-header">
      <h3>外观主题</h3>
    </div>
    <p class="export-hint">选择深色 / 浅色 / 跟随系统主题。</p>

    <el-radio-group v-model="theme.mode" size="large" style="margin-bottom: 16px;">
      <el-radio value="dark">
        <div class="theme-option">
          <Icon icon="ph:moon-stars" width="20" />
          <span>深色模式</span>
        </div>
      </el-radio>
      <el-radio value="light">
        <div class="theme-option">
          <Icon icon="ph:sun" width="20" />
          <span>浅色模式</span>
        </div>
      </el-radio>
      <el-radio value="auto">
        <div class="theme-option">
          <Icon icon="ph:desktop" width="20" />
          <span>跟随系统</span>
        </div>
      </el-radio>
    </el-radio-group>

    <el-form label-width="120px" class="premium-form">
      <el-form-item label="当前主题">
        <el-text type="primary">{{ theme.resolvedTheme === 'dark' ? '深色' : '浅色' }}</el-text>
      </el-form-item>

      <el-form-item :label="$t('settings.language')">
        <el-radio-group v-model="lang" size="large">
          <el-radio value="zh-CN">简体中文</el-radio>
          <el-radio value="en-US">English</el-radio>
        </el-radio-group>
      </el-form-item>
    </el-form>
  </div>
</template>

<script setup lang="ts">
import { watch } from 'vue'
import { ref } from 'vue'
import { Icon } from '@iconify/vue'
import { useThemeStore } from '@/stores/theme'
import { useI18n } from '@/composables/useI18n'

const theme = useThemeStore()
const { setLocale } = useI18n()
const lang = ref(localStorage.getItem('minefolio_lang') || 'zh-CN')

watch(() => theme.mode, (newMode) => {
  theme.setMode(newMode)
})

watch(() => lang.value, (value) => {
  if (value === 'zh-CN' || value === 'en-US') setLocale(value)
})
</script>

<style scoped>
.theme-option {
  display: flex;
  align-items: center;
  gap: 8px;
}

::deep(.el-radio__label) {
  display: flex !important;
  align-items: center !important;
  padding-left: 8px !important;
  font-size: 14px !important;
}
</style>
