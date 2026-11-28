<template>
  <div class="asset-detail-page" v-loading="loading">
    <PageHeader :title="assetDetail?.name || '资产详情'">
      <template #actions>
        <el-button @click="$router.back()">返回</el-button>
      </template>
    </PageHeader>

    <div v-if="assetDetail" class="detail-content">
      <!-- 核心指标卡片 -->
      <div class="metric-grid">
        <div class="metric-card">
          <div class="metric-label">市值</div>
          <div class="metric-value">{{ formatCurrency(assetDetail.current_value, assetDetail.currency) }}</div>
        </div>
        <div class="metric-card">
          <div class="metric-label">持有份额</div>
          <div class="metric-value">{{ assetDetail.quantity?.toFixed(4) }}</div>
        </div>
        <div class="metric-card">
          <div class="metric-label">摊薄成本</div>
          <div class="metric-value">
            {{ assetDetail.quantity > 0 ? (assetDetail.cost_basis / assetDetail.quantity).toFixed(4) : '—' }}
          </div>
        </div>
        <div class="metric-card">
          <div class="metric-label">最新净值</div>
          <div class="metric-value">{{ assetDetail.net_value?.toFixed(4) }}</div>
        </div>
      </div>

      <!-- 盈亏指标行 -->
      <div class="metric-grid">
        <div class="metric-card">
          <div class="metric-label">浮动盈亏</div>
          <div :class="['metric-value', pnlClass(unrealized)]">{{ formatCurrency(unrealized, assetDetail.currency) }}</div>
        </div>
        <div class="metric-card">
          <div class="metric-label">浮动收益率</div>
          <div :class="['metric-value', pnlClass(unrealized)]">
            {{ unrealizedPct }}%
          </div>
        </div>
        <div class="metric-card">
          <div class="metric-label">组合占比</div>
          <div class="metric-value">{{ ((assetDetail.portfolio_weight ?? 0) * 100).toFixed(1) }}%</div>
        </div>
        <div class="metric-card">
          <div class="metric-label">当日涨跌</div>
          <div :class="['metric-value', pnlClass(dailyChange?.pct ?? 0)]">
            <template v-if="dailyChange">
              {{ dailyChange.pct >= 0 ? '+' : '' }}{{ dailyChange.pct?.toFixed(2) }}%
            </template>
            <template v-else>—</template>
          </div>
        </div>
      </div>

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
        <div class="txn-table-wrap">
          <el-table :data="assetDetail.transactions" size="small" max-height="400" style="width: 100%; min-width: 540px">
            <el-table-column prop="transaction_date" label="日期" width="104" />
            <el-table-column prop="transaction_type" label="类型" width="80" />
            <el-table-column prop="quantity" label="数量" width="84" align="right">
              <template #default="{ row }">
                <span class="mf-amount-mask">{{ row.quantity ?? '—' }}</span>
              </template>
            </el-table-column>
            <el-table-column prop="price_per_unit" label="单价" width="88" align="right">
              <template #default="{ row }">
                <span class="mf-amount-mask">{{ row.price_per_unit ?? '—' }}</span>
              </template>
            </el-table-column>
            <el-table-column prop="amount" label="金额" min-width="100" align="right">
              <template #default="{ row }">
                <span class="mf-amount-mask">{{ formatCurrency(row.amount, row.currency || assetDetail?.currency) }}</span>
              </template>
            </el-table-column>
            <el-table-column prop="note" label="备注" min-width="120" show-overflow-tooltip />
            <template #empty>
              <el-empty description="暂无交易流水" :image-size="64" />
            </template>
          </el-table>
        </div>
      </div>
    </div>

    <el-alert
      v-else-if="loadError"
      title="无法获取资产详情（当前离线或服务端不可达）"
      type="warning"
      :closable="false"
      show-icon
    />

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
import PageHeader from '@/components/PageHeader.vue'
import AssetPriceLine from '@/components/AssetPriceLine.vue'
import IntervalReturnTabs from '@/components/IntervalReturnTabs.vue'
import { formatCurrency } from '@/utils/format'
import { isNetworkError } from '@/utils/http'
import type { AssetDetailResponse, PriceHistoryItem } from '@/types'

const route = useRoute()
const loading = ref(true)
const loadError = ref(false)
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
  if (val > 0) return 'gain-text'
  if (val < 0) return 'loss-text'
  return 'zero-text'
}

async function loadAsset() {
  try {
    assetDetail.value = (await assetsApi.detail(assetId.value)) as unknown as AssetDetailResponse
    loadError.value = false
  } catch (err) {
    if (isNetworkError(err)) {
      loadError.value = true
    } else if (!assetDetail.value && (err as any)?.response?.status !== 404) {
      loadError.value = true
    }
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
  if (!Number.isFinite(assetId.value) || assetId.value <= 0) {
    loading.value = false
    return
  }
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
  display: flex;
  flex-direction: column;
  gap: 16px;
  max-width: 100%;
  overflow-x: hidden;
  box-sizing: border-box;
}

.detail-content {
  display: flex;
  flex-direction: column;
  gap: 16px;
  min-width: 0;
  max-width: 100%;
}

.metric-grid {
  display: grid;
  grid-template-columns: repeat(4, minmax(0, 1fr));
  gap: 12px;
  flex-shrink: 0;
}

@media (max-width: 900px) {
  .metric-grid {
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }
}

.metric-card {
  min-width: 0;
  background: var(--mf-surface-muted);
  border: 1px solid var(--mf-border);
  border-radius: var(--mf-radius-md);
  padding: 10px 14px;
  box-sizing: border-box;
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
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.gain-text { color: var(--mf-gain); }
.loss-text { color: var(--mf-loss); }

.interval-card,
.chart-card,
.txn-card {
  min-width: 0;
  max-width: 100%;
  overflow: hidden;
  background: var(--mf-surface);
  border: 1px solid var(--mf-border);
  border-radius: var(--mf-radius-lg);
  padding: 16px 20px;
  box-sizing: border-box;
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

.txn-table-wrap {
  width: 100%;
  overflow-x: auto;
  -webkit-overflow-scrolling: touch;
}

@media (max-width: 640px) {
  .metric-value {
    font-size: 15px;
  }
  .interval-card,
  .chart-card,
  .txn-card {
    padding: 12px 14px;
  }
}
</style>
