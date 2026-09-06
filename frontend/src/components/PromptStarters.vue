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
    </div>

    <div class="starters-grid">
      <div
        v-for="(item, idx) in starters"
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
import { Icon } from '@iconify/vue'

defineEmits<{
  (e: 'select', prompt: string): void
}>()

const starters = [
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
</script>

<style scoped>
.prompt-starters {
  display: flex;
  flex-direction: column;
  align-items: center;
  max-width: 860px;
  margin: 0 auto;
  padding: 32px 16px;
}

.starters-header {
  text-align: center;
  margin-bottom: 28px;
}

.sparkle-icon-wrap {
  width: 48px;
  height: 48px;
  border-radius: 14px;
  background: linear-gradient(135deg, var(--el-color-primary), var(--el-color-primary-dark-2));
  display: flex;
  align-items: center;
  justify-content: center;
  margin: 0 auto 14px;
  box-shadow: 0 6px 18px rgba(99, 102, 241, 0.25);
}

.sparkle-icon {
  font-size: 24px;
  color: #fff;
}

.starters-title {
  font-size: 22px;
  font-weight: 700;
  color: var(--el-text-color-primary);
  margin-bottom: 8px;
}

.starters-subtitle {
  font-size: 13px;
  color: var(--el-text-color-secondary);
  max-width: 540px;
  line-height: 1.6;
}

.starters-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(250px, 1fr));
  gap: 14px;
  width: 100%;
}

.starter-card {
  display: flex;
  flex-direction: column;
  justify-content: space-between;
  padding: 16px;
  border-radius: 14px;
  background: var(--el-bg-color-overlay);
  border: 1px solid var(--el-border-color-lighter);
  cursor: pointer;
  transition: all 0.22s ease-in-out;
  box-shadow: 0 2px 10px rgba(0, 0, 0, 0.03);
}

.starter-card:hover {
  transform: translateY(-3px);
  border-color: var(--el-color-primary-light-5);
  box-shadow: 0 8px 24px rgba(0, 0, 0, 0.07);
}

.card-top {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 12px;
}

.card-icon {
  width: 36px;
  height: 36px;
  border-radius: 10px;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 18px;
}

.card-tag {
  font-size: 11px;
  font-weight: 500;
  padding: 2px 8px;
  border-radius: 6px;
  background: var(--el-fill-color-light);
  color: var(--el-text-color-secondary);
}

.card-title {
  font-size: 14px;
  font-weight: 600;
  color: var(--el-text-color-primary);
  margin-bottom: 6px;
}

.card-desc {
  font-size: 12px;
  color: var(--el-text-color-secondary);
  line-height: 1.5;
  margin-bottom: 14px;
}

.card-footer {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding-top: 10px;
  border-top: 1px solid var(--el-border-color-extra-light);
  font-size: 12px;
  color: var(--el-color-primary);
  font-weight: 500;
}

.arrow-icon {
  font-size: 13px;
  transition: transform 0.2s ease;
}

.starter-card:hover .arrow-icon {
  transform: translateX(4px);
}

@media (max-width: 640px) {
  .starters-grid {
    grid-template-columns: 1fr;
  }
}
</style>
