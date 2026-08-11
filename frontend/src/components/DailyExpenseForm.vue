<template>
  <el-form :model="form" label-width="90px">
    <el-form-item label="类型">
      <el-radio-group v-model="form.expense_type">
        <el-radio value="income">收入</el-radio>
        <el-radio value="expense">支出</el-radio>
      </el-radio-group>
    </el-form-item>
    <el-form-item label="金额">
      <el-input-number v-model="form.amount" :precision="2" :min="0" style="width: 100%" />
    </el-form-item>
    <el-form-item label="日期">
      <el-date-picker v-model="form.expense_date" type="date" value-format="YYYY-MM-DD" style="width: 100%" />
    </el-form-item>
    <el-form-item label="备注">
      <el-input v-model="form.note" type="textarea" :rows="2" />
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
