<template>
  <div class="prompt-starters">
    <div class="starters-header">
      <div class="sparkle-icon-wrap">
        <Icon icon="ph:sparkle-fill" class="sparkle-icon" />
      </div>
      <h3 class="starters-title">Minefolio 智能财务助手</h3>
      <p class="starters-subtitle">
        基于大模型与实时财务账单引擎，为您提供全景健康体检、自然语言快捷记账与深度理财规划。
      </p>

      <!-- 财务上下文全景速览胶囊 -->
      <div v-if="summaryData" class="context-summary-capsule">
        <div class="capsule-item">
          <span class="capsule-label">净资产</span>
          <span class="capsule-value net-worth">{{ formatCurrency(summaryData.net_worth) }}</span>
        </div>
        <div class="capsule-divider"></div>
        <div class="capsule-item">
          <span class="capsule-label">总资产</span>
          <span class="capsule-value">{{ formatCurrency(summaryData.total_assets) }}</span>
        </div>
        <div class="capsule-divider"></div>
        <div class="capsule-item">
          <span class="capsule-label">资产负债率</span>
          <span class="capsule-value" :class="{ 'warning-debt': debtRatio > 40 }">
            {{ summaryData.total_liabilities === 0 ? '零负债' : (summaryData.total_assets > 0 ? `${debtRatio.toFixed(1)}%` : '0.0%') }}
          </span>
        </div>
        <div class="capsule-status-tag">
          <span class="status-pulse-dot"></span>
          <span>已连通实时账本</span>
        </div>
      </div>
    </div>

    <div class="starters-grid">
      <div
        v-for="(item, idx) in dynamicStarters"
        :key="idx"
        class="starter-card"
        :class="item.tag"
        @click="$emit('select', item.prompt as string)"
      >
        <div class="card-top">
          <div class="card-icon" :style="{ backgroundColor: item.bg, color: item.color }">
            <Icon :icon="item.icon" />
          </div>
          <span class="card-tag">{{ item.tagLabel }}</span>
        </div>
        <div class="card-body">
          <h4 class="card-title">{{ item.title }}</h4>
          <p class="card-desc">{{ item.desc }}</p>
        </div>
        <div class="card-footer">
          <span class="action-hint">点击发送指令</span>
          <Icon icon="ph:arrow-right-bold" class="arrow-icon" />
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, computed } from 'vue'
import { Icon } from '@iconify/vue'
import { summaryApi } from '@/api/summary'
import type { Summary } from '@/types'
import { formatCurrency } from '@/utils/format'

defineEmits<{
  (e: 'select', prompt: string): void
}>()

const summaryData = ref<Summary | null>(null)
const loadingSummary = ref(false)

onMounted(async () => {
  loadingSummary.value = true
  try {
    const res = await summaryApi.get()
    summaryData.value = res as unknown as Summary
  } catch {
    summaryData.value = null
  } finally {
    loadingSummary.value = false
  }
})

const debtRatio = computed(() => {
  if (!summaryData.value || summaryData.value.total_assets <= 0) return 0
  return (summaryData.value.total_liabilities / summaryData.value.total_assets) * 100
})

interface StarterItem {
  icon: string
  title: string
  desc: string
  tag: string
  tagLabel: string
  bg: string
  color: string
  prompt: string
}

const classicStarters: StarterItem[] = [
  {
    icon: 'ph:shield-check-bold',
    title: '全套财务健康体检',
    desc: '一键诊断应急金月数、储蓄率、资产负债率与投资占比',
    tag: 'health',
    tagLabel: '诊断体检',
    bg: 'rgba(99, 102, 241, 0.12)',
    color: '#6366f1',
    prompt: '请对我的财务状况做一次全面的健康体检，计算流动性备用金月数、净储蓄率、资产负债率与生息资产占比，并附上专业优化建议与 Mermaid 结构图。',
  },
  {
    icon: 'ph:shopping-cart-bold',
    title: '自然语言快捷记账',
    desc: '智能识别金额、分类与账户，生成交互卡片确认入库',
    tag: 'action',
    tagLabel: '智能记账',
    bg: 'rgba(16, 185, 129, 0.12)',
    color: '#10b981',
    prompt: '今天在超市购物花费 86.5 元，从微信钱包扣款，请帮我生成记账草稿。',
  },
  {
    icon: 'ph:chart-pie-slice-bold',
    title: '本月收支结构洞察',
    desc: '深度分析本月支出构成，指出占比最高的三大分类',
    tag: 'insight',
    tagLabel: '收支洞察',
    bg: 'rgba(245, 158, 11, 0.12)',
    color: '#f59e0b',
    prompt: '请分析我本月的收支情况，统计各项支出占比与收支结余，并用 Mermaid 饼图直观展示。',
  },
  {
    icon: 'ph:trend-up-bold',
    title: '定投复利收益测算',
    desc: '测算每月定投终值、累计本金与复利增长曲线',
    tag: 'calc',
    tagLabel: '理财计算',
    bg: 'rgba(14, 165, 233, 0.12)',
    color: '#0ea5e9',
    prompt: '如果我每月定投 2500 元，年化预期收益率 8%，定投 10 年后的本金与总收益分别是多少？请列出详细年份对照。',
  },
  {
    icon: 'ph:currency-circle-dollar-bold',
    title: '实时外汇汇率查询',
    desc: '直连全球外汇市场，获取 USD/EUR/JPY/HKD 汇率',
    tag: 'forex',
    tagLabel: '外汇行情',
    bg: 'rgba(236, 72, 153, 0.12)',
    color: '#ec4899',
    prompt: '查询今日美元兑人民币、欧元兑人民币、日元兑人民币的实时汇率与换算对比。',
  },
]

const dynamicStarters = computed<StarterItem[]>(() => {
  const s = summaryData.value
  if (!s) return classicStarters

  const list: StarterItem[] = []

  // 1. 若有负债且负债率 > 15%，优先推荐负债优化与抗风险诊断
  if (s.total_liabilities > 0 && debtRatio.value > 15) {
    list.push({
      icon: 'ph:shield-warning-bold',
      title: '负债健康度与还款建议',
      desc: `当前负债率 ${debtRatio.value.toFixed(1)}% (总负债 ${formatCurrency(s.total_liabilities)})，诊断利息负担与提前还款`,
      tag: 'debt',
      tagLabel: '负债诊断',
      bg: 'rgba(239, 68, 68, 0.12)',
      color: '#ef4444',
      prompt: `我当前的财务数据为：总资产 ${formatCurrency(s.total_assets)}，总负债 ${formatCurrency(s.total_liabilities)}，净资产 ${formatCurrency(s.net_worth)}，当前资产负债率为 ${debtRatio.value.toFixed(1)}%。请帮我全面评估当前的偿债压力，分析利息支出对现金流的影响，并给出稳健的还款与抗风险建议。`,
    })
  }

  // 2. 若总资产 > 0，推荐全景资产结构与再平衡
  if (s.total_assets > 0) {
    list.push({
      icon: 'ph:chart-pie-slice-bold',
      title: '全景资产配置与抗风险诊断',
      desc: `基于当前净资产 ${formatCurrency(s.net_worth)}，测算资产分布韧性与流动性应急金`,
      tag: 'health',
      tagLabel: '资产诊断',
      bg: 'rgba(99, 102, 241, 0.12)',
      color: '#6366f1',
      prompt: `我目前的总资产为 ${formatCurrency(s.total_assets)}，总负债为 ${formatCurrency(s.total_liabilities)}，净资产为 ${formatCurrency(s.net_worth)}。请帮我进行全景资产配置健康度体检，测算应急流动金月数与生息资产占比，并附上调仓建议与 Mermaid 结构图。`,
    })
  } else {
    // 3. 资产为 0 的新用户引导
    list.push({
      icon: 'ph:plant-bold',
      title: '新用户首张财务报表建档',
      desc: '从零搭建个人资产负债表与月度收支预算底座',
      tag: 'onboard',
      tagLabel: '快速建档',
      bg: 'rgba(16, 185, 129, 0.12)',
      color: '#10b981',
      prompt: '我是刚开始使用 Minefolio 记录个人财务的新用户，请指导我如何科学搭建第一张个人资产负债表，并梳理合理的储蓄与预算结构。',
    })
  }

  // 4. 自然语言快捷记账
  list.push({
    icon: 'ph:shopping-cart-bold',
    title: '自然语言快捷记账',
    desc: '智能识别金额、分类与账户，生成交互卡片确认入库',
    tag: 'action',
    tagLabel: '智能记账',
    bg: 'rgba(16, 185, 129, 0.12)',
    color: '#10b981',
    prompt: '今天在超市购物花费 86.5 元，从微信钱包扣款，请帮我生成记账草稿。',
  })

  // 5. 定投复利计算
  list.push({
    icon: 'ph:trend-up-bold',
    title: '定投复利收益测算',
    desc: '测算每月定投终值、累计本金与复利增长曲线',
    tag: 'calc',
    tagLabel: '理财计算',
    bg: 'rgba(14, 165, 233, 0.12)',
    color: '#0ea5e9',
    prompt: '如果我每月定投 2500 元，年化预期收益率 8%，定投 10 年后的本金与总收益分别是多少？请列出详细年份对照。',
  })

  // 6. 外汇行情
  list.push({
    icon: 'ph:currency-circle-dollar-bold',
    title: '实时外汇汇率查询',
    desc: '直连全球外汇市场，获取 USD/EUR/JPY/HKD 汇率',
    tag: 'forex',
    tagLabel: '外汇行情',
    bg: 'rgba(236, 72, 153, 0.12)',
    color: '#ec4899',
    prompt: '查询今日美元兑人民币、欧元兑人民币、日元兑人民币的实时汇率与换算对比。',
  })

  return list
})
</script>

<style scoped>
.prompt-starters {
  display: flex;
  flex-direction: column;
  align-items: center;
  width: 100%;
  max-width: 100%;
  box-sizing: border-box;
  margin: 0 auto;
  padding: 24px 0 32px;
}

.starters-header {
  text-align: center;
  margin-bottom: 24px;
}

.sparkle-icon-wrap {
  width: 52px;
  height: 52px;
  border-radius: 16px;
  background: linear-gradient(135deg, var(--mf-primary), #6366f1);
  display: flex;
  align-items: center;
  justify-content: center;
  margin: 0 auto 16px;
  box-shadow: 0 4px 16px rgba(99, 102, 241, 0.25), var(--mf-shadow-glow);
}

.sparkle-icon {
  font-size: 26px;
  color: #ffffff;
}

.starters-title {
  font-size: 24px;
  font-weight: 700;
  color: var(--mf-text-main);
  letter-spacing: -0.3px;
  margin-bottom: 8px;
}

.starters-subtitle {
  font-size: 13.5px;
  color: var(--mf-text-muted);
  max-width: 560px;
  line-height: 1.6;
}

/* 财务上下文全景速览胶囊 */
.context-summary-capsule {
  display: inline-flex;
  align-items: center;
  gap: 14px;
  margin-top: 16px;
  padding: 8px 18px;
  border-radius: 30px;
  background: var(--mf-surface);
  border: 1px solid var(--mf-border);
  box-shadow: var(--mf-shadow-sm);
  font-size: 12.5px;
  flex-wrap: wrap;
  justify-content: center;
}

.capsule-item {
  display: flex;
  align-items: center;
  gap: 6px;
}

.capsule-label {
  color: var(--mf-text-muted);
  font-weight: 500;
}

.capsule-value {
  color: var(--mf-text-main);
  font-weight: 600;
  font-family: var(--mf-font-mono, monospace);
}

.capsule-value.net-worth {
  color: var(--mf-primary);
  font-weight: 700;
}

.capsule-value.warning-debt {
  color: var(--mf-danger, #ef4444);
}

.capsule-divider {
  width: 1px;
  height: 12px;
  background: var(--mf-border);
}

.capsule-status-tag {
  display: flex;
  align-items: center;
  gap: 5px;
  padding: 2px 8px;
  border-radius: 12px;
  background: var(--mf-surface-hover, rgba(0, 0, 0, 0.04));
  color: var(--mf-text-secondary);
  font-size: 11px;
}

.status-pulse-dot {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: var(--mf-success, #16a34a);
  box-shadow: 0 0 6px rgba(22, 163, 74, 0.4);
}

.starters-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(256px, 1fr));
  gap: 16px;
  width: 100%;
}

.starter-card {
  display: flex;
  flex-direction: column;
  justify-content: space-between;
  padding: 18px;
  border-radius: 16px;
  background: var(--mf-surface);
  border: 1px solid var(--mf-border);
  backdrop-filter: blur(16px);
  cursor: pointer;
  transition: transform 0.22s cubic-bezier(0.4, 0, 0.2, 1), border-color 0.22s, box-shadow 0.22s;
  box-shadow: var(--mf-shadow-sm);
  position: relative;
  overflow: hidden;
}

.starter-card::before {
  content: '';
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  height: 2px;
  background: transparent;
  transition: background 0.25s ease;
}

.starter-card:hover {
  transform: translateY(-3px);
  border-color: var(--mf-primary);
  box-shadow: var(--mf-shadow-md), 0 0 20px rgba(0, 212, 255, 0.08);
}

.starter-card:hover::before {
  background: linear-gradient(90deg, transparent, var(--mf-primary), transparent);
}

.card-top {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 14px;
}

.card-icon {
  width: 40px;
  height: 40px;
  border-radius: 12px;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 20px;
  box-shadow: inset 0 0 0 1px rgba(255, 255, 255, 0.1);
}

.card-tag {
  font-size: 11px;
  font-weight: 600;
  padding: 3px 8px;
  border-radius: 20px;
  background: var(--mf-surface-muted);
  color: var(--mf-text-muted);
  border: 1px solid var(--mf-border);
}

.card-title {
  font-size: 14.5px;
  font-weight: 600;
  color: var(--mf-text-main);
  margin-bottom: 6px;
  letter-spacing: -0.2px;
}

.card-desc {
  font-size: 12px;
  color: var(--mf-text-muted);
  line-height: 1.55;
  margin-bottom: 16px;
}

.card-footer {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding-top: 12px;
  border-top: 1px solid var(--mf-border);
  font-size: 12px;
  color: var(--mf-primary);
  font-weight: 600;
}

.action-hint {
  font-size: 11.5px;
  opacity: 0.85;
}

.arrow-icon {
  font-size: 14px;
  transition: transform 0.2s ease;
}

.starter-card:hover .arrow-icon {
  transform: translateX(4px);
}

@media (max-width: 640px) {
  .starters-grid {
    grid-template-columns: 1fr;
  }
  .context-summary-capsule {
    gap: 8px;
    padding: 8px 12px;
  }
}
</style>
