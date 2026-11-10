<template>
  <div class="asset-detail-page" v-loading="loading">
    <div class="page-header">
      <div class="header-title">
        <div class="title-accent"></div>
        <h2>{{ assetDetail?.name || '资产详情' }}</h2>
      </div>
      <div class="header-actions">
        <el-button @click="$router.back()">返回</el-button>
      </div>
    </div>

    <div v-if="assetDetail" class="detail-content">
      <!-- 核心指标卡片 -->
      <el-row :gutter="12" class="metric-row">
        <el-col :span="6">
          <div class="metric-card">
            <div class="metric-label">市值</div>
            <div class="metric-value">{{ formatCurrency(assetDetail.current_value) }}</div>
          </div>
        </el-col>
        <el-col :span="6">
          <div class="metric-card">
            <div class="metric-label">持有份额</div>
            <div class="metric-value">{{ assetDetail.quantity?.toFixed(4) }}</div>
          </div>
        </el-col>
        <el-col :span="6">
          <div class="metric-card">
            <div class="metric-label">摊薄成本</div>
            <div class="metric-value">
              {{ assetDetail.quantity > 0 ? (assetDetail.cost_basis / assetDetail.quantity).toFixed(4) : '—' }}
            </div>
          </div>
        </el-col>
        <el-col :span="6">
          <div class="metric-card">
            <div class="metric-label">最新净值</div>
            <div class="metric-value">{{ assetDetail.net_value?.toFixed(4) }}</div>
          </div>
        </el-col>
      </el-row>

      <!-- 盈亏指标行 -->
      <el-row :gutter="12" class="metric-row">
        <el-col :span="6">
          <div class="metric-card">
            <div class="metric-label">浮动盈亏</div>
            <div :class="['metric-value', pnlClass(unrealized)]">{{ formatCurrency(unrealized) }}</div>
          </div>
        </el-col>
        <el-col :span="6">
          <div class="metric-card">
            <div class="metric-label">浮动收益率</div>
            <div :class="['metric-value', pnlClass(unrealized)]">
              {{ unrealizedPct }}%
            </div>
          </div>
        </el-col>
        <el-col :span="6">
          <div class="metric-card">
            <div class="metric-label">组合占比</div>
            <div class="metric-value">{{ ((assetDetail.portfolio_weight ?? 0) * 100).toFixed(1) }}%</div>
          </div>
        </el-col>
        <el-col :span="6">
          <div class="metric-card">
            <div class="metric-label">当日涨跌</div>
            <div :class="['metric-value', pnlClass(dailyChange?.pct ?? 0)]">
              <template v-if="dailyChange">
                {{ dailyChange.pct >= 0 ? '+' : '' }}{{ dailyChange.pct?.toFixed(2) }}%
              </template>
              <template v-else>—</template>
            </div>
          </div>
        </el-col>
      </el-row>

      <!-- 区间收益 tab -->
      <div v-if="assetDetail.interval_returns" class="interval-card">
        <h3>区间收益率</h3>
        <IntervalReturnTabs :data="assetDetail.interval_returns" />
      </div>

      <!-- 净值走势曲线 -->
      <div class="chart-card">
        <div class="card-header">
          <h3>净值走势（近一年）</h3>
        </div>
        <div class="chart-container">
          <AssetPriceLine :data="priceHistory" />
        </div>
      </div>

      <!-- 交易流水 -->
      <div class="txn-card">
        <h3>交易流水</h3>
        <el-table :data="assetDetail.transactions" size="small" max-height="400">
          <el-table-column prop="transaction_date" label="日期" width="120" />
          <el-table-column prop="transaction_type" label="类型" width="100" />
          <el-table-column prop="quantity" label="数量" width="100" align="right" />
          <el-table-column prop="price_per_unit" label="单价" width="100" align="right" />
          <el-table-column prop="amount" label="金额" min-width="120" align="right" />
          <el-table-column prop="note" label="备注" min-width="150" show-overflow-tooltip />
        </el-table>
      </div>
    </div>

    <div v-else-if="!loading" class="mf-empty-state">
      <div class="mf-empty-circle"></div>
      <p>资产不存在</p>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import { useRoute } from 'vue-router'
import { assetsApi } from '@/api/assets'
import { marketApi } from '@/api/market'
import AssetPriceLine from '@/components/AssetPriceLine.vue'
import IntervalReturnTabs from '@/components/IntervalReturnTabs.vue'
import { formatCurrency } from '@/utils/format'
import type { AssetDetailResponse, PriceHistoryItem } from '@/types'

const route = useRoute()
const loading = ref(true)
const assetDetail = ref<AssetDetailResponse | null>(null)
const priceHistory = ref<PriceHistoryItem[]>([])

const assetId = computed(() => Number(route.params.id))

const unrealized = computed(() => {
  if (!assetDetail.value) return 0
  return assetDetail.value.current_value - assetDetail.value.cost_basis
})

const unrealizedPct = computed(() => {
  if (!assetDetail.value || assetDetail.value.cost_basis <= 0) return 0
  return ((unrealized.value / assetDetail.value.cost_basis) * 100).toFixed(2)
})

const dailyChange = computed(() => assetDetail.value?.daily_change ?? null)

function pnlClass(val: number): string {
  return val >= 0 ? 'gain-text' : 'loss-text'
}

async function loadAsset() {
  try {
    assetDetail.value = (await assetsApi.detail(assetId.value)) as unknown as AssetDetailResponse
  } catch {
    /* ignore */
  }
}

async function loadPriceHistory() {
  try {
    priceHistory.value = await marketApi.priceHistory(assetId.value, 365)
  } catch {
    /* ignore */
  }
}

onMounted(async () => {
  loading.value = true
  try {
    await Promise.allSettled([loadAsset(), loadPriceHistory()])
  } finally {
    loading.value = false
  }
})
</script>

<style scoped>
.asset-detail-page {
  padding: 16px;
  height: 100%;
  overflow: auto;
}

.detail-content {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.metric-row {
  flex-shrink: 0;
}

.metric-card {
  background: var(--mf-surface-muted);
  border: 1px solid var(--mf-border);
  border-radius: var(--mf-radius-md);
  padding: 10px 14px;
}

.metric-label {
  font-size: 11px;
  color: var(--mf-text-muted);
  margin-bottom: 4px;
}

.metric-value {
  font-size: 18px;
  font-weight: 700;
  font-family: 'JetBrains Mono', monospace;
}

.gain-text { color: var(--mf-gain); }
.loss-text { color: var(--mf-loss); }

.interval-card,
.chart-card,
.txn-card {
  background: var(--mf-surface);
  border: 1px solid var(--mf-border);
  border-radius: var(--mf-radius-lg);
  padding: 16px 20px;
}

.interval-card h3,
.chart-card h3,
.txn-card h3 {
  margin: 0 0 12px;
  font-size: 14px;
  font-weight: 600;
  color: var(--mf-text-main);
}

.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 12px;
}

.chart-container {
  height: 220px;
}

.mf-empty-state {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: 60px 24px;
  gap: 16px;
}

.mf-empty-circle {
  width: 56px;
  height: 56px;
  border-radius: 50%;
  background: radial-gradient(circle at 30% 30%, var(--mf-primary-light), var(--mf-accent-light));
  border: 1px solid var(--mf-primary-border);
}

.mf-empty-state p {
  color: var(--mf-text-muted);
  font-size: 13px;
  margin: 0;
}
</style>
