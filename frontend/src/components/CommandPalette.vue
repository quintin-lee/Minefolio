<template>
  <div v-if="modelValue" class="command-palette-backdrop" @click="close">
    <div class="command-palette-dialog" @click.stop>
      <!-- Top Search Input Bar -->
      <div class="command-input-wrapper">
        <Icon icon="ph:magnifying-glass-bold" class="search-icon" />
        <input
          ref="inputRef"
          v-model="query"
          type="text"
          class="command-search-input"
          placeholder="搜索页面、执行快捷动作或记账... (支持拼音与英文)"
          @keydown.down.prevent="navigate(1)"
          @keydown.up.prevent="navigate(-1)"
          @keydown.enter.prevent="executeSelected"
          @keydown.esc="close"
        />
        <span class="esc-badge" @click="close">ESC</span>
      </div>

      <!-- Results / Commands List -->
      <div class="command-list-wrapper">
        <div v-if="filteredCommands.length === 0" class="empty-results">
          <Icon icon="ph:mask-sad-duotone" class="empty-icon" />
          <span>未找到相关指令或页面</span>
        </div>

        <template v-else>
          <div
            v-for="(group, gIdx) in groupedCommands"
            :key="group.title"
            class="command-group"
          >
            <div class="group-title">{{ group.title }}</div>
            <div
              v-for="item in group.items"
              :key="item.id"
              class="command-item"
              :class="{ active: item.globalIndex === selectedIndex }"
              @mouseenter="selectedIndex = item.globalIndex"
              @click="handleSelect(item)"
            >
              <div class="item-left">
                <span class="item-icon-box">
                  <Icon :icon="item.icon" class="item-icon" />
                </span>
                <div class="item-meta">
                  <span class="item-label">{{ item.label }}</span>
                  <span v-if="item.description" class="item-desc">{{ item.description }}</span>
                </div>
              </div>
              <div class="item-right">
                <span v-if="item.shortcut" class="item-shortcut">{{ item.shortcut }}</span>
                <Icon icon="ph:arrow-elbow-down-left-bold" class="enter-icon" />
              </div>
            </div>
          </div>
        </template>
      </div>

      <!-- Footer Hints -->
      <div class="command-palette-footer">
        <div class="footer-hint">
          <span class="key-cap">↑</span>
          <span class="key-cap">↓</span>
          <span>选择</span>
        </div>
        <div class="footer-hint">
          <span class="key-cap">↵</span>
          <span>执行</span>
        </div>
        <div class="footer-hint">
          <span class="key-cap">ESC</span>
          <span>关闭</span>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch, nextTick, onMounted, onBeforeUnmount } from 'vue'
import { useRouter } from 'vue-router'
import { Icon } from '@iconify/vue'
import { useThemeStore } from '@/stores/theme'
import { useMarketStore } from '@/stores/market'

interface CommandItem {
  id: string
  label: string
  description?: string
  keywords: string[]
  icon: string
  group: 'actions' | 'navigation'
  action?: () => void
  route?: string
  shortcut?: string
}

const props = defineProps<{
  modelValue: boolean
}>()

const emit = defineEmits<{
  (e: 'update:modelValue', val: boolean): void
  (e: 'quick-record'): void
}>()

const router = useRouter()
const themeStore = useThemeStore()
const marketStore = useMarketStore()

const inputRef = ref<HTMLInputElement | null>(null)
const query = ref('')
const selectedIndex = ref(0)

const commands = computed<CommandItem[]>(() => [
  // Quick Actions
  {
    id: 'quick-add',
    label: '快捷记账',
    description: '快速录入支出、收入或资产转账记录',
    keywords: ['记账', '支出', '收入', '转账', 'add', 'record', 'quick'],
    icon: 'ph:plus-circle-duotone',
    group: 'actions',
    action: () => emit('quick-record'),
    shortcut: 'N',
  },
  {
    id: 'sync-market',
    label: '全网行情一键同步',
    description: '刷新股票指数、离岸汇率与加密标的最新价格',
    keywords: ['行情', '同步', '刷新', '汇率', 'sync', 'market', 'quote'],
    icon: 'ph:arrows-clockwise-duotone',
    group: 'actions',
    action: () => {
      marketStore.syncAllQuotes()
    },
  },
  {
    id: 'toggle-theme',
    label: '切换深色 / 浅色模式',
    description: '切换界面日间微白与极光暗夜 FinTech 主题',
    keywords: ['主题', '深色', '浅色', '暗夜', 'theme', 'dark', 'light'],
    icon: 'ph:moon-stars-duotone',
    group: 'actions',
    action: () => {
      themeStore.toggle()
    },
  },

  // Navigation Items
  {
    id: 'nav-dashboard',
    label: '仪表盘',
    description: '核心资产总览、多币种统计与近期待办',
    keywords: ['仪表盘', '首页', 'dashboard', 'home', 'networth'],
    icon: 'ph:squares-four-duotone',
    group: 'navigation',
    route: '/dashboard',
  },
  {
    id: 'nav-assets',
    label: '资产管理',
    description: '银行卡、证券、基金账户与负债管理',
    keywords: ['资产', '账户', '现金', '银行', 'assets', 'bank'],
    icon: 'ph:vault-duotone',
    group: 'navigation',
    route: '/assets',
  },
  {
    id: 'nav-holdings',
    label: '持仓分析',
    description: '投资标的成本市值对比与浮动盈亏',
    keywords: ['持仓', '股票', '基金', '盈亏', 'holdings', 'stocks', 'pnl'],
    icon: 'ph:trend-up-duotone',
    group: 'navigation',
    route: '/holdings',
  },
  {
    id: 'nav-transactions',
    label: '交易明细',
    description: '买入、卖出、分红与仓位流水',
    keywords: ['交易', '买入', '卖出', '分红', 'transactions', 'trades'],
    icon: 'ph:arrows-left-right-duotone',
    group: 'navigation',
    route: '/transactions',
  },
  {
    id: 'nav-daily-expenses',
    label: '日常收支',
    description: '生活流水开销明细与月度收支统计',
    keywords: ['收支', '开销', '流水', '日常', 'expenses', 'daily'],
    icon: 'ph:receipt-duotone',
    group: 'navigation',
    route: '/daily-expenses',
  },
  {
    id: 'nav-plans',
    label: '定投计划与现金流日历',
    description: '周期定投策略监控与固定账单日历',
    keywords: ['计划', '定投', '日历', 'dca', 'plans', 'schedule'],
    icon: 'ph:target-duotone',
    group: 'navigation',
    route: '/plans',
  },
  {
    id: 'nav-reports',
    label: '财务报表',
    description: '收支结余月报、年度汇总与分类占比',
    keywords: ['报表', '月报', '年报', '分析', 'reports', 'charts'],
    icon: 'ph:presentation-chart-duotone',
    group: 'navigation',
    route: '/reports',
  },
  {
    id: 'nav-market',
    label: '市场行情中心',
    description: '全球指数大盘、交易时钟与实时外汇换算',
    keywords: ['行情', '大盘', '外汇', '汇率', 'market', 'forex', 'fx'],
    icon: 'ph:chart-line-up-duotone',
    group: 'navigation',
    route: '/market',
  },
  {
    id: 'nav-categories',
    label: '分类体系设置',
    description: '收支多级分类与资产标签管理',
    keywords: ['分类', '标签', '类别', 'categories', 'tags'],
    icon: 'ph:tree-structure-duotone',
    group: 'navigation',
    route: '/categories',
  },
  {
    id: 'nav-chat',
    label: 'AI 财务对话助手',
    description: '智能持仓诊断、流水智能分析与策略建议',
    keywords: ['ai', '对话', '问答', 'chat', 'assistant', 'gpt'],
    icon: 'ph:sparkle-duotone',
    group: 'navigation',
    route: '/chat',
  },
  {
    id: 'nav-ai-traces',
    label: 'AI 调用追踪',
    description: 'LLM 运行日志、工具耗时与 Policy 审计',
    keywords: ['追踪', 'trace', 'logs', 'audit'],
    icon: 'ph:activity-duotone',
    group: 'navigation',
    route: '/ai-traces',
  },
  {
    id: 'nav-settings',
    label: '系统与安全设置',
    description: '密码修改、2FA 双因子认证与账本配置',
    keywords: ['设置', '密码', '安全', 'settings', 'config'],
    icon: 'ph:sliders-horizontal-duotone',
    group: 'navigation',
    route: '/settings',
  },
])

const filteredCommands = computed(() => {
  const q = query.value.trim().toLowerCase()
  if (!q) return commands.value
  return commands.value.filter((item) => {
    return (
      item.label.toLowerCase().includes(q) ||
      (item.description && item.description.toLowerCase().includes(q)) ||
      (item.route && item.route.toLowerCase().includes(q)) ||
      item.keywords.some((k) => k.toLowerCase().includes(q))
    )
  })
})

const groupedCommands = computed(() => {
  const list = filteredCommands.value
  const groups: { title: string; items: (CommandItem & { globalIndex: number })[] }[] = []
  let counter = 0

  const actions = list
    .filter((c) => c.group === 'actions')
    .map((c) => ({ ...c, globalIndex: counter++ }))
  if (actions.length > 0) {
    groups.push({ title: '快捷动作', items: actions })
  }

  const navs = list
    .filter((c) => c.group === 'navigation')
    .map((c) => ({ ...c, globalIndex: counter++ }))
  if (navs.length > 0) {
    groups.push({ title: '导航直达', items: navs })
  }

  return groups
})

watch(
  () => props.modelValue,
  (val) => {
    if (val) {
      query.value = ''
      selectedIndex.value = 0
      nextTick(() => {
        inputRef.value?.focus()
      })
    }
  }
)

watch(filteredCommands, () => {
  selectedIndex.value = 0
})

function close() {
  emit('update:modelValue', false)
}

function navigate(direction: number) {
  const total = filteredCommands.value.length
  if (total === 0) return
  selectedIndex.value = (selectedIndex.value + direction + total) % total
}

function handleSelect(item: CommandItem) {
  close()
  if (item.action) {
    item.action()
  } else if (item.route) {
    router.push(item.route)
  }
}

function executeSelected() {
  const item = filteredCommands.value[selectedIndex.value]
  if (item) {
    handleSelect(item)
  }
}
</script>

<style scoped>
.command-palette-backdrop {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.45);
  backdrop-filter: blur(6px);
  z-index: 3000;
  display: flex;
  align-items: flex-start;
  justify-content: center;
  padding-top: 12vh;
  animation: fadeIn 0.18s cubic-bezier(0.16, 1, 0.3, 1);
}

.command-palette-dialog {
  width: 580px;
  max-width: 90vw;
  background: var(--mf-surface-card);
  border: 1px solid var(--mf-border);
  box-shadow: 0 20px 40px -10px rgba(0, 0, 0, 0.35), 0 0 0 1px rgba(255, 255, 255, 0.05);
  border-radius: var(--mf-radius-lg);
  overflow: hidden;
  display: flex;
  flex-direction: column;
  animation: slideDown 0.18s cubic-bezier(0.16, 1, 0.3, 1);
}

.command-input-wrapper {
  display: flex;
  align-items: center;
  padding: 14px 18px;
  border-bottom: 1px solid var(--mf-border-subtle);
  gap: 12px;
}

.search-icon {
  font-size: 20px;
  color: var(--mf-text-muted);
  flex-shrink: 0;
}

.command-search-input {
  flex: 1;
  background: transparent;
  border: none;
  outline: none;
  font-size: 15px;
  color: var(--mf-text-main);
  font-family: inherit;
}

.command-search-input::placeholder {
  color: var(--mf-text-muted);
  font-size: 14px;
}

.esc-badge {
  font-size: 11px;
  font-weight: 600;
  color: var(--mf-text-muted);
  background: var(--mf-surface-muted);
  border: 1px solid var(--mf-border-subtle);
  padding: 2px 6px;
  border-radius: var(--mf-radius-sm);
  cursor: pointer;
}

.command-list-wrapper {
  max-height: 380px;
  overflow-y: auto;
  padding: 8px 10px;
}

.empty-results {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: 36px 0;
  color: var(--mf-text-muted);
  gap: 8px;
  font-size: 14px;
}

.empty-icon {
  font-size: 32px;
  opacity: 0.6;
}

.command-group {
  margin-bottom: 8px;
}

.group-title {
  font-size: 11px;
  font-weight: 700;
  text-transform: uppercase;
  color: var(--mf-text-muted);
  padding: 6px 10px;
  letter-spacing: 0.05em;
}

.command-item {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 9px 12px;
  border-radius: var(--mf-radius-md);
  cursor: pointer;
  transition: background 0.12s ease;
}

.command-item.active {
  background: var(--mf-surface-hover);
}

.item-left {
  display: flex;
  align-items: center;
  gap: 12px;
}

.item-icon-box {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 28px;
  height: 28px;
  border-radius: var(--mf-radius-sm);
  background: var(--mf-surface-muted);
  color: var(--mf-primary);
  flex-shrink: 0;
}

.item-icon {
  font-size: 16px;
}

.item-meta {
  display: flex;
  flex-direction: column;
}

.item-label {
  font-size: 14px;
  font-weight: 500;
  color: var(--mf-text-main);
}

.item-desc {
  font-size: 12px;
  color: var(--mf-text-muted);
  margin-top: 1px;
}

.item-right {
  display: flex;
  align-items: center;
  gap: 8px;
}

.item-shortcut {
  font-size: 11px;
  font-family: var(--mf-font-mono);
  color: var(--mf-text-muted);
  background: var(--mf-surface-muted);
  border: 1px solid var(--mf-border-subtle);
  padding: 1px 5px;
  border-radius: var(--mf-radius-sm);
}

.enter-icon {
  font-size: 14px;
  color: var(--mf-text-muted);
  opacity: 0;
  transition: opacity 0.12s ease;
}

.command-item.active .enter-icon {
  opacity: 1;
  color: var(--mf-primary);
}

.command-palette-footer {
  display: flex;
  align-items: center;
  gap: 16px;
  padding: 10px 18px;
  background: var(--mf-surface-muted);
  border-top: 1px solid var(--mf-border-subtle);
  font-size: 12px;
  color: var(--mf-text-muted);
}

.footer-hint {
  display: flex;
  align-items: center;
  gap: 6px;
}

.key-cap {
  background: var(--mf-surface-card);
  border: 1px solid var(--mf-border-subtle);
  padding: 1px 5px;
  border-radius: 3px;
  font-size: 10px;
  font-family: var(--mf-font-mono);
  font-weight: 600;
}

@keyframes fadeIn {
  from { opacity: 0; }
  to { opacity: 1; }
}

@keyframes slideDown {
  from {
    opacity: 0;
    transform: translateY(-8px) scale(0.98);
  }
  to {
    opacity: 1;
    transform: translateY(0) scale(1);
  }
}
</style>
