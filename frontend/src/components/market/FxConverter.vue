<template>
  <el-card shadow="never" class="converter-card">
    <template #header>
      <div class="card-header">
        <span class="header-title">💱 {{ t('market.converter.title') }}</span>
        <span class="header-formula">{{ formulaText }}</span>
      </div>
    </template>

    <div class="converter-body">
      <!-- Source Amount Row -->
      <div class="amount-row">
        <div class="row-label">{{ t('market.converter.sourceAmount') }}</div>
        <div class="row-inputs">
          <el-input-number
            v-model="sourceAmount"
            :min="0"
            :precision="2"
            :controls="false"
            class="amount-input"
            placeholder="0.00"
          />
          <el-select v-model="fromCurrency" class="currency-select">
            <el-option
              v-for="c in currencyList"
              :key="c.code"
              :label="`${c.flag} ${c.code}`"
              :value="c.code"
            />
          </el-select>
        </div>
      </div>

      <!-- Swap Action Button -->
      <div class="swap-action">
        <el-button circle class="swap-btn" @click="handleSwap">
          <span class="swap-icon">⇅</span>
        </el-button>
      </div>

      <!-- Target Converted Row -->
      <div class="amount-row">
        <div class="row-label">{{ t('market.converter.targetAmount') }}</div>
        <div class="row-inputs">
          <el-input
            :model-value="convertedAmountFormatted"
            readonly
            class="amount-input converted-result"
          >
            <template #append>
              <el-button @click="handleCopy">
                📋
              </el-button>
            </template>
          </el-input>
          <el-select v-model="toCurrency" class="currency-select">
            <el-option
              v-for="c in currencyList"
              :key="c.code"
              :label="`${c.flag} ${c.code}`"
              :value="c.code"
            />
          </el-select>
        </div>
      </div>

      <!-- Quick Preset Amounts -->
      <div class="presets-row">
        <span class="presets-label">{{ t('market.converter.quickAmounts') }}:</span>
        <div class="preset-tags">
          <el-tag
            v-for="amt in [100, 500, 1000, 5000, 10000]"
            :key="amt"
            class="preset-tag"
            effect="plain"
            @click="sourceAmount = amt"
          >
            {{ amt.toLocaleString() }}
          </el-tag>
        </div>
      </div>
    </div>
  </el-card>
</template>

<script setup lang="ts">
import { ref, computed } from 'vue'
import { useI18n } from 'vue-i18n'
import { ElMessage } from 'element-plus'
import { useMarketStore } from '@/stores/market'
import { convertCurrency, calcCrossRate, formatRate } from '@/utils/fx'

const { t } = useI18n()
const marketStore = useMarketStore()

const currencyList = [
  { code: 'CNY', name: '人民币', flag: '🇨🇳' },
  { code: 'USD', name: '美元', flag: '🇺🇸' },
  { code: 'EUR', name: '欧元', flag: '🇪🇺' },
  { code: 'HKD', name: '港币', flag: '🇭🇰' },
  { code: 'JPY', name: '日元', flag: '🇯🇵' },
  { code: 'GBP', name: '英镑', flag: '🇬🇧' },
  { code: 'AUD', name: '澳元', flag: '🇦🇺' },
  { code: 'CAD', name: '加元', flag: '🇨🇦' },
  { code: 'SGD', name: '新加坡元', flag: '🇸🇬' },
]

const sourceAmount = ref<number>(1000)
const fromCurrency = ref<string>('USD')
const toCurrency = ref<string>('CNY')

const convertedAmount = computed(() => {
  return convertCurrency(sourceAmount.value, fromCurrency.value, toCurrency.value, marketStore.rates)
})

const convertedAmountFormatted = computed(() => {
  return convertedAmount.value.toLocaleString(undefined, {
    minimumFractionDigits: 2,
    maximumFractionDigits: 4,
  })
})

const formulaText = computed(() => {
  const rate = calcCrossRate(fromCurrency.value, toCurrency.value, marketStore.rates)
  return t('market.converter.formula', {
    from: fromCurrency.value,
    to: toCurrency.value,
    rate: formatRate(rate),
  })
})

function handleSwap() {
  const tmpCur = fromCurrency.value
  fromCurrency.value = toCurrency.value
  toCurrency.value = tmpCur
  sourceAmount.value = Math.round(convertedAmount.value)
}

async function handleCopy() {
  try {
    await navigator.clipboard.writeText(convertedAmountFormatted.value)
    ElMessage.success(t('market.cards.copySuccess'))
  } catch {
    ElMessage.info(convertedAmountFormatted.value)
  }
}
</script>

<style scoped>
.converter-card {
  border-radius: 10px;
  height: 100%;
}

.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.header-title {
  font-size: 15px;
  font-weight: 600;
  color: var(--el-text-color-primary);
}

.header-formula {
  font-size: 12px;
  color: var(--el-color-primary);
  font-weight: 500;
}

.converter-body {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.amount-row {
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.row-label {
  font-size: 12px;
  color: var(--el-text-color-secondary);
}

.row-inputs {
  display: flex;
  gap: 8px;
}

.amount-input {
  flex: 1;
}

.amount-input :deep(.el-input__wrapper) {
  background: var(--mf-surface-muted);
  box-shadow: 0 0 0 1px var(--mf-border-subtle) inset;
  border-radius: var(--mf-radius-md);
}
.amount-input :deep(.el-input__wrapper.is-focus) {
  box-shadow: 0 0 0 1px var(--mf-primary) inset;
}

.converted-result :deep(input) {
  font-weight: 700;
  color: var(--el-color-primary);
  font-size: 16px;
}

.currency-select {
  width: 140px;
}
.currency-select :deep(.el-select__wrapper) {
  background: var(--mf-surface-muted);
  box-shadow: 0 0 0 1px var(--mf-border-subtle) inset;
  border-radius: var(--mf-radius-md);
}

.swap-action {
  display: flex;
  justify-content: center;
  margin: -4px 0;
}

.swap-btn {
  background: var(--mf-surface-muted);
  border: 1px solid var(--mf-border-subtle);
  color: var(--el-color-primary);
}

.swap-icon {
  font-size: 16px;
  font-weight: bold;
}

.presets-row {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-top: 4px;
}

.presets-label {
  font-size: 12px;
  color: var(--el-text-color-secondary);
}

.preset-tags {
  display: flex;
  gap: 6px;
  flex-wrap: wrap;
}

.preset-tag {
  cursor: pointer;
  font-size: 11px;
  background: var(--mf-surface-muted);
  border: 1px solid var(--mf-border-subtle);
}

.preset-tag:hover {
  color: var(--el-color-primary);
  border-color: var(--el-color-primary);
}
</style>
