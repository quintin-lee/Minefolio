<template>
  <el-tooltip :content="tooltip" placement="bottom" :show-after="300">
    <button class="amount-toggle-btn" @click="toggle" :aria-label="tooltip">
      <Icon :icon="currentIcon" class="amount-icon" />
    </button>
  </el-tooltip>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { Icon } from '@iconify/vue'
import { useDisplaySettingsStore } from '@/stores/display-settings'
import { t } from '@/utils/locale'

const store = useDisplaySettingsStore()

const currentIcon = computed(() => {
  return store.showAmount ? 'ph:eye' : 'ph:eye-closed'
})

const tooltip = computed(() => {
  return store.showAmount
    ? t('settings.amountMaskHintOff')
    : t('settings.amountMaskHintOn')
})

function toggle() {
  store.toggle()
}
</script>

<style scoped>
.amount-toggle-btn {
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

.amount-toggle-btn:hover {
  background: var(--mf-surface-hover);
  border-color: var(--mf-border-hover);
  color: var(--mf-primary);
  transform: translateY(-1px);
  box-shadow: var(--mf-shadow-sm);
}

.amount-icon {
  font-size: 17px;
  transition: transform 0.2s ease;
}

.amount-toggle-btn:hover .amount-icon {
  transform: scale(1.1);
}
</style>
