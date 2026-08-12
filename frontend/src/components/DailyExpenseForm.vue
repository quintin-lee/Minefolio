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
  color: #475569;
}

.type-radio-group :deep(.el-radio-button__inner) {
  border-radius: 8px !important;
  margin-right: 12px;
  border: 1px solid rgba(0, 212, 255, 0.15);
  box-shadow: none !important;
  padding: 8px 20px;
  font-weight: 500;
  background: rgba(15, 23, 42, 0.4);
  color: #94a3b8;
}

.type-radio-group :deep(.el-radio-button.is-active .el-radio-button__inner) {
  background: linear-gradient(135deg, #00d4ff, #0ea5e9) !important;
  border-color: #00d4ff !important;
  color: #060b18 !important;
}

.premium-form :deep(.el-form-item__label) {
  font-weight: 500;
  color: #94a3b8;
}

.premium-form :deep(.el-input__wrapper) {
  background-color: rgba(15, 23, 42, 0.4) !important;
  box-shadow: 0 0 0 1px rgba(0, 212, 255, 0.12) inset !important;
  border-radius: 8px;
  padding: 6px 12px;
}
</style>
