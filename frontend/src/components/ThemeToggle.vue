<template>
  <el-tooltip :content="themeTooltip" placement="bottom" :show-after="300">
    <button class="theme-toggle-btn" @click="toggleTheme" :aria-label="themeTooltip">
      <Icon :icon="currentIcon" class="theme-icon" />
    </button>
  </el-tooltip>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { Icon } from '@iconify/vue'
import { useThemeStore } from '@/stores/theme'

const theme = useThemeStore()

const currentIcon = computed(() => {
  if (theme.mode === 'auto') {
    return 'ph:desktop'
  }
  return theme.resolvedTheme === 'dark' ? 'ph:moon-stars' : 'ph:sun'
})

const themeTooltip = computed(() => {
  if (theme.mode === 'auto') return '跟随系统主题 (点击切换模式)'
  return theme.resolvedTheme === 'dark' ? '深色模式 (点击切换为浅色)' : '浅色模式 (点击切换为深色)'
})

function toggleTheme() {
  theme.toggle()
}
</script>

<style scoped>
.theme-toggle-btn {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 34px;
  height: 34px;
  border-radius: var(--mf-radius-md);
  background: var(--mf-surface-muted);
  border: 1px solid var(--mf-border);
  color: var(--mf-text-regular);
  cursor: pointer;
  transition: var(--mf-transition);
  padding: 0;
  outline: none;
}

.theme-toggle-btn:hover {
  background: var(--mf-surface-hover);
  border-color: var(--mf-border-hover);
  color: var(--mf-primary);
  transform: translateY(-1px);
  box-shadow: var(--mf-shadow-sm);
}

.theme-icon {
  font-size: 17px;
  transition: transform 0.3s cubic-bezier(0.34, 1.56, 0.64, 1);
}

.theme-toggle-btn:hover .theme-icon {
  transform: rotate(15deg) scale(1.1);
}
</style>
