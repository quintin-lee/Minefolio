<template>
  <div v-if="visible" class="asset-mention-menu" role="menu" aria-label="快捷资产与账户引用">
    <div class="menu-header">
      <Icon icon="ph:at-bold" class="header-icon" />
      <span>引用财务账户/资产 (按 ↑↓ 导航，Enter 确认，Esc 退出)</span>
    </div>
    <div class="menu-list" ref="listRef">
      <div
        v-for="(item, idx) in filteredItems"
        :key="`${item.type}-${item.id}`"
        class="menu-item"
        :class="{ active: selectedIndex === idx }"
        @click="selectItem(item)"
        @mouseenter="selectedIndex = idx"
      >
        <div class="item-icon" :class="item.type">
          <Icon :icon="item.icon" />
        </div>
        <div class="item-info">
          <div class="item-title">{{ item.name }}</div>
          <div v-if="item.subText" class="item-desc">{{ item.subText }}</div>
        </div>
        <span class="item-tag" :class="item.type">{{ item.tag }}</span>
      </div>
      <div v-if="filteredItems.length === 0" class="menu-empty">
        未找到匹配的账户或资产
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch, nextTick, onMounted } from 'vue'
import { Icon } from '@iconify/vue'
import { accountsApi } from '@/api/accounts'
import { assetsApi } from '@/api/assets'

export interface MentionItem {
  id: string | number
  name: string
  subText?: string
  type: 'account' | 'asset'
  icon: string
  tag: string
}

const props = defineProps<{
  visible: boolean
  query: string
}>()

const emit = defineEmits<{
  (e: 'select', item: MentionItem): void
  (e: 'close'): void
}>()

const items = ref<MentionItem[]>([])
const selectedIndex = ref(0)
const listRef = ref<HTMLElement | null>(null)

function getAssetIcon(type?: string): string {
  if (type === 'crypto') return 'ph:currency-btc-bold'
  if (type === 'stock') return 'ph:chart-line-up-bold'
  if (type === 'fund') return 'ph:chart-pie-slice-bold'
  if (type === 'bond') return 'ph:certificate-bold'
  return 'ph:coins-bold'
}

async function loadData() {
  try {
    const [accRes, assetRes] = await Promise.allSettled([
      accountsApi.list(),
      assetsApi.list({ page_size: 50 }),
    ])

    const next: MentionItem[] = []

    if (accRes.status === 'fulfilled' && Array.isArray(accRes.value)) {
      for (const a of accRes.value) {
        next.push({
          id: a.id,
          name: a.name,
          subText: `${a.institution || '账户'} · ${a.asset_count || 0}项资产`,
          type: 'account',
          icon: a.icon || 'ph:bank-bold',
          tag: '账户容器',
        })
      }
    }

    if (assetRes.status === 'fulfilled' && Array.isArray(assetRes.value?.list)) {
      for (const a of assetRes.value.list) {
        next.push({
          id: a.id,
          name: a.name,
          subText: `估值 ¥${Number(a.current_value || 0).toLocaleString()} · ${a.account_name || '默认账户'}`,
          type: 'asset',
          icon: getAssetIcon(a.asset_type),
          tag: a.category_name || '资产',
        })
      }
    }

    items.value = next
  } catch {
    // ignore
  }
}

onMounted(() => {
  loadData()
})

const filteredItems = computed(() => {
  const q = props.query.trim().toLowerCase()
  if (!q) return items.value
  return items.value.filter(item =>
    item.name.toLowerCase().includes(q) ||
    (item.subText && item.subText.toLowerCase().includes(q)) ||
    item.tag.toLowerCase().includes(q)
  )
})

watch(() => props.query, () => {
  selectedIndex.value = 0
})

function selectItem(item: MentionItem) {
  emit('select', item)
}

function scrollToActive() {
  nextTick(() => {
    const el = listRef.value?.querySelector('.menu-item.active') as HTMLElement | null
    if (el && listRef.value) {
      el.scrollIntoView({ block: 'nearest' })
    }
  })
}

function handleKeyDown(e: KeyboardEvent): boolean {
  if (!props.visible) return false
  if (e.key === 'ArrowDown') {
    e.preventDefault()
    selectedIndex.value = (selectedIndex.value + 1) % Math.max(1, filteredItems.value.length)
    scrollToActive()
    return true
  } else if (e.key === 'ArrowUp') {
    e.preventDefault()
    selectedIndex.value = (selectedIndex.value - 1 + filteredItems.value.length) % Math.max(1, filteredItems.value.length)
    scrollToActive()
    return true
  } else if (e.key === 'Enter') {
    e.preventDefault()
    if (filteredItems.value[selectedIndex.value]) {
      selectItem(filteredItems.value[selectedIndex.value]!)
    }
    return true
  } else if (e.key === 'Escape') {
    e.preventDefault()
    emit('close')
    return true
  }
  return false
}

defineExpose({ handleKeyDown })
</script>

<style scoped>
.asset-mention-menu {
  position: absolute;
  bottom: calc(100% + 8px);
  left: 0;
  width: 380px;
  max-width: 90vw;
  background: var(--mf-background-elevated);
  backdrop-filter: blur(14px);
  border: 1px solid var(--mf-primary-border);
  border-radius: 10px;
  box-shadow: var(--mf-shadow-lg);
  overflow: hidden;
  z-index: 100;
  animation: slideUp 0.15s ease-out;
}

@keyframes slideUp {
  from { opacity: 0; transform: translateY(6px); }
  to { opacity: 1; transform: translateY(0); }
}

.menu-header {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 8px 12px;
  font-size: 11px;
  color: var(--mf-primary);
  background: var(--mf-primary-light);
  border-bottom: 1px solid var(--mf-primary-border);
  font-weight: 500;
}

.header-icon {
  font-size: 13px;
}

.menu-list {
  max-height: min(240px, 32vh);
  overflow-y: auto;
  padding: 4px;
}

.menu-item {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 7px 10px;
  border-radius: 6px;
  cursor: pointer;
  transition: background 0.15s ease;
}

.menu-item:hover, .menu-item.active {
  background: var(--mf-primary-light);
}

.item-icon {
  width: 26px;
  height: 26px;
  border-radius: 6px;
  background: var(--mf-primary-light);
  color: var(--mf-primary);
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 14px;
  flex-shrink: 0;
}

.item-icon.account {
  background: rgba(16, 185, 129, 0.12);
  color: #10b981;
}

.item-icon.asset {
  background: var(--mf-primary-light);
  color: var(--mf-primary);
}

.item-info {
  flex: 1;
  min-width: 0;
}

.item-title {
  font-size: 12.5px;
  font-weight: 600;
  color: var(--mf-text-main);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.item-desc {
  font-size: 11px;
  color: var(--mf-text-muted);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.item-tag {
  font-size: 10px;
  padding: 1px 6px;
  border-radius: 8px;
  background: var(--mf-surface-hover);
  color: var(--mf-text-muted);
  flex-shrink: 0;
}

.item-tag.account {
  color: #10b981;
  background: rgba(16, 185, 129, 0.1);
}

.item-tag.asset {
  color: var(--mf-primary);
  background: var(--mf-primary-light);
}

.menu-empty {
  padding: 16px;
  text-align: center;
  font-size: 12px;
  color: var(--mf-text-muted);
}
</style>
