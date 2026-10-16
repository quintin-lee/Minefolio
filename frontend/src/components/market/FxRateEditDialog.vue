<template>
  <el-dialog
    v-model="visible"
    :title="t('market.editRate.dialogTitle')"
    width="420px"
    destroy-on-close
  >
    <el-form label-position="top">
      <el-form-item :label="t('market.editRate.currencyLabel')">
        <el-select v-model="formCurrency" style="width: 100%;">
          <el-option
            v-for="c in currencies"
            :key="c"
            :label="c"
            :value="c"
          />
        </el-select>
      </el-form-item>

      <el-form-item :label="t('market.editRate.rateLabel')">
        <el-input-number
          v-model="formRate"
          :precision="4"
          :step="0.01"
          :min="0.0001"
          style="width: 100%;"
          :placeholder="t('market.editRate.ratePlaceholder')"
        />
      </el-form-item>
    </el-form>

    <template #footer>
      <div class="dialog-footer">
        <el-button @click="visible = false">{{ t('common.cancel') }}</el-button>
        <el-button type="primary" :loading="isSaving" @click="handleSave">
          {{ t('common.save') }}
        </el-button>
      </div>
    </template>
  </el-dialog>
</template>

<script setup lang="ts">
import { ref, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import { ElMessage } from 'element-plus'
import { useMarketStore } from '@/stores/market'

const { t } = useI18n()
const marketStore = useMarketStore()

const visible = ref(false)
const formCurrency = ref('USD')
const formRate = ref(7.2345)
const isSaving = ref(false)

const currencies = ['USD', 'EUR', 'HKD', 'JPY', 'GBP', 'AUD', 'CAD', 'SGD']

watch(formCurrency, (cur) => {
  formRate.value = marketStore.rates[cur] || 1.0
})

function open(cur = 'USD') {
  formCurrency.value = cur
  formRate.value = marketStore.rates[cur] || 1.0
  visible.value = true
}

async function handleSave() {
  if (formRate.value <= 0) return
  isSaving.value = true
  try {
    await marketStore.updateRate(formCurrency.value, formRate.value)
    ElMessage.success(t('market.editRate.saveSuccess'))
    visible.value = false
  } catch (err) {
    ElMessage.error(t('common.error'))
  } finally {
    isSaving.value = false
  }
}

defineExpose({ open })
</script>
