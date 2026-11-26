/**
 * @file 从本地 SQLite 聚合月度收支报表（口径对齐 ExpenseMonthly）
 */

import { query, rowsFrom } from '@/db/local'
import type { ExpenseMonthly, ExpenseType } from '@/types'

function activeLedgerId(): number {
  return Number(localStorage.getItem('minefolio_active_ledger_id') || 0)
}

/** 按后端 ExpenseMonthly 口径聚合指定年月的本地数据 */
export function aggregateMonthlyReport(year: number, month: number): ExpenseMonthly {
  const prefix = `${year}-${String(month).padStart(2, '0')}-%`
  const ledgerId = activeLedgerId()

  const rows = rowsFrom(query(
    `SELECT de.category_id, de.expense_type, de.amount, de.expense_date, c.name AS category_name
     FROM daily_expenses de
     LEFT JOIN categories c ON c.id = de.category_id
     WHERE de.__deleted = 0 AND de.expense_date LIKE ?
       AND (de.ledger_id = ? OR de.ledger_id = 0)`,
    [prefix, ledgerId]
  ))

  let totalIncome = 0
  let totalExpense = 0
  const categoryMap = new Map<number, { name: string; type: ExpenseType; amount: number }>()
  const dayMap = new Map<string, { income: number; expense: number }>()

  for (const r of rows) {
    const amount = Number(r.amount ?? 0)
    const type: ExpenseType = r.expense_type === 'income' ? 'income' : 'expense'
    if (type === 'income') totalIncome += amount
    else totalExpense += amount

    const catId = Number(r.category_id ?? 0)
    const cat = categoryMap.get(catId) ?? { name: String(r.category_name ?? ''), type, amount: 0 }
    cat.amount += amount
    categoryMap.set(catId, cat)

    const date = String(r.expense_date ?? '')
    const day = dayMap.get(date) ?? { income: 0, expense: 0 }
    if (type === 'income') day.income += amount
    else day.expense += amount
    dayMap.set(date, day)
  }

  const byCategory = [...categoryMap.entries()]
    .map(([id, c]) => ({
      name: c.name || `分类 ${id}`,
      type: c.type,
      amount: c.amount,
      pct: totalExpense > 0 ? Math.round((c.amount / totalExpense) * 1000) / 10 : 0,
    }))
    .filter((c) => c.type === 'expense' && c.amount > 0)
    .sort((a, b) => b.amount - a.amount)

  const tagRows = rowsFrom(query(
    `SELECT t.name AS tag_name, COUNT(*) AS cnt, SUM(de.amount) AS amount
     FROM daily_expenses de
     JOIN expense_tags et ON et.expense_id = de.id AND et.__deleted = 0
     JOIN tags t ON t.id = et.tag_id AND t.__deleted = 0
     WHERE de.__deleted = 0 AND de.expense_type = 'expense' AND de.expense_date LIKE ?
       AND (de.ledger_id = ? OR de.ledger_id = 0)
     GROUP BY t.id, t.name`,
    [prefix, ledgerId]
  ))

  const byTag = tagRows.map((r) => ({
    tag_name: String(r.tag_name ?? ''),
    amount: Number(r.amount ?? 0),
    count: Number(r.cnt ?? 0),
  }))

  const dailyBreakdown = [...dayMap.entries()]
    .map(([date, d]) => ({ date, income: d.income, expense: d.expense }))
    .sort((a, b) => a.date.localeCompare(b.date))

  return {
    year,
    month,
    total_income: totalIncome,
    total_expense: totalExpense,
    balance: totalIncome - totalExpense,
    by_category: byCategory,
    by_tag: byTag,
    daily_breakdown: dailyBreakdown,
  }
}
