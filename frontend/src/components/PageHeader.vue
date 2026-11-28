<template>
  <div class="mf-page-header page-header" :class="{ 'is-compact': compact }">
    <div class="mf-page-header-left">
      <div class="mf-page-header-heading">
        <div class="header-title">
          <Icon v-if="icon" :icon="icon" class="title-icon" />
          <div v-else class="title-accent"></div>
          <h1 class="mf-page-title">{{ title }}</h1>
        </div>
        <div v-if="$slots.extra" class="mf-page-header-extra">
          <slot name="extra" />
        </div>
      </div>
      <p v-if="subtitle" class="page-subtitle">{{ subtitle }}</p>
    </div>
    <div v-if="$slots.actions" class="header-actions">
      <slot name="actions" />
    </div>
  </div>
</template>

<script setup lang="ts">
import { Icon } from '@iconify/vue'

defineProps<{
  title: string
  subtitle?: string
  icon?: string
  compact?: boolean
}>()
</script>

<style scoped>
.mf-page-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  flex-wrap: wrap;
  gap: 12px 16px;
  flex-shrink: 0;
  margin-bottom: 0;
}

.mf-page-header.is-compact {
  margin-bottom: 0;
}

.mf-page-header-left {
  display: flex;
  flex-direction: column;
  gap: 4px;
  min-width: 0;
  max-width: 760px;
}

.mf-page-header-heading {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 12px 20px;
  min-width: 0;
}

.header-title {
  display: flex;
  align-items: center;
  gap: 10px;
  min-width: 0;
}

.title-accent {
  width: 4px;
  height: 18px;
  background: linear-gradient(180deg, var(--mf-primary) 0%, var(--mf-accent) 100%);
  border-radius: 2px;
  flex-shrink: 0;
}

.title-icon {
  font-size: 24px;
  color: var(--mf-primary);
  flex-shrink: 0;
}

.mf-page-title {
  font-size: 20px;
  font-weight: 700;
  color: var(--mf-text-main);
  margin: 0;
  letter-spacing: -0.3px;
  line-height: 1.3;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.mf-page-header-extra {
  display: flex;
  align-items: center;
  min-width: 0;
}

.page-subtitle {
  font-size: 13px;
  color: var(--mf-text-secondary);
  line-height: 1.5;
  margin: 0;
}

.header-actions {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 8px;
  margin-left: auto;
}

@media (max-width: 768px) {
  .mf-page-header {
    flex-direction: column;
    align-items: stretch;
    gap: 10px;
  }

  .mf-page-title {
    font-size: 18px;
  }

  .page-subtitle {
    display: none;
  }

  .header-actions {
    width: 100%;
    margin-left: 0;
  }
}
</style>
