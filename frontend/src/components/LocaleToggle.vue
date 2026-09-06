<template>
  <button class="locale-toggle" type="button" :title="locale === 'en-US' ? '切换中文' : 'Switch to English'" @click="toggle">
    {{ locale === 'en-US' ? '中' : 'EN' }}
  </button>
</template>

<script setup lang="ts">
import { useI18n } from '@/composables/useI18n'

const { locale, setLocale } = useI18n()
function toggle() {
  setLocale(locale.value === 'en-US' ? 'zh-CN' : 'en-US')
}
</script>

<style scoped>
.locale-toggle {
  background: var(--mf-surface);
  border: 1px solid var(--mf-border);
  color: var(--mf-text-muted);
  border-radius: 8px;
  font-size: 12px;
  font-weight: 600;
  line-height: 1;
  padding: 6px 9px;
  cursor: pointer;
  transition: var(--mf-transition);
}
.locale-toggle:hover {
  color: var(--mf-primary);
  border-color: var(--mf-primary-border);
}
</style>
