<template>
  <el-form :model="form" label-width="90px" class="premium-form">
    <el-form-item label="类型">
      <el-radio-group v-model="form.expense_type" class="type-radio-group">
        <el-radio-button value="income">收入</el-radio-button>
        <el-radio-button value="expense">支出</el-radio-button>
      </el-radio-group>
    </el-form-item>
    <el-form-item label="金额">
      <el-input-number v-model="form.amount" :precision="2" :min="0" style="width: 100%" :controls="false" />
    </el-form-item>
    <el-form-item label="日期">
      <el-date-picker v-model="form.expense_date" type="date" value-format="YYYY-MM-DD" style="width: 100%" />
    </el-form-item>
    <el-form-item label="备注">
      <el-input v-model="form.note" type="textarea" :rows="3" placeholder="添加备注..." />
    </el-form-item>
  </el-form>
</template>

<script setup lang="ts">
export interface DailyExpenseFormModel {
  expense_type: 'income' | 'expense'
  amount: number
  expense_date: string
  note: string
}

const form = defineModel<DailyExpenseFormModel>({ required: true })
</script>

<style scoped>
.premium-form .el-form-item {
  margin-bottom: 24px;
}

.premium-form :deep(.el-form-item__label) {
  font-weight: 500;
  color: var(--mf-text-muted);
}

.type-radio-group :deep(.el-radio-button__inner) {
  border-radius: 8px !important;
  margin-right: 12px;
  border: 1px solid var(--mf-primary-border);
  box-shadow: none !important;
  padding: 8px 20px;
  font-weight: 500;
  background: var(--mf-surface-muted);
  color: var(--mf-text-muted);
}

.type-radio-group :deep(.el-radio-button.is-active .el-radio-button__inner) {
  background: linear-gradient(135deg, var(--mf-primary) 0%, var(--mf-primary-hover) 100%) !important;
  border-color: var(--mf-primary) !important;
  color: #ffffff !important;
}

.premium-form :deep(.el-input__wrapper) {
  background-color: var(--mf-surface-muted) !important;
  box-shadow: 0 0 0 1px var(--mf-primary-light) inset !important;
  border-radius: 8px;
  padding: 6px 12px;
}
</style>
