# 仪表盘月度收支图改为年度图 — 设计文档

- 日期: 2026-08-16
- 范围: 后端新增 yearly 聚合接口 + 前端新增年度图表组件并接入仪表盘
- 状态: 已获用户批准

## 1. 需求

仪表盘「月度收支」卡片改为「年度收支」：按自然年 1-12 月展示收入/支出柱状图，头部为年份选择器（默认当前年）。保留月度明细能力（收支页不受影响）。

## 2. 现状

- 仪表盘卡片：`el-date-picker type="month"` + `MonthlyChart`（单月双柱，x 轴写死 `['本月']`）
- 数据源：`dailyExpensesApi.monthly(year, month)`（单月汇总）
- 后端已有 `GET /api/reports/expense/trend?months=N`（滚动窗口、无零补齐），但用户确认为**自然年 1-12 月**口径，需新接口。

## 3. 设计

### 3.1 后端：`GET /api/reports/expense/yearly?year=YYYY`

- `report_expense_yearly()`（`backend/src/reports.c`），year 缺省为当前年。
- SQL：`SELECT CAST(SUBSTR(expense_date,6,2) AS INTEGER) AS m, SUM(income..), SUM(expense..) FROM daily_expenses WHERE user_id=? AND SUBSTR(expense_date,1,4)=? GROUP BY m ORDER BY m`（参数化，与现有接口同风格）。
- C 代码零补齐 1-12 月（无数据月份补 0）。
- 响应：`{ labels: ["1月".."12月"], income: [12 个数], expense: [12 个数] }`。
- 路由注册：`backend/src/main.c`（extern 声明 + `csilk_app_get`）。

### 3.2 前端

| 文件 | 改动 |
|------|------|
| `frontend/src/types/index.ts` | 新增 `ExpenseYearlyReport { labels: string[]; income: number[]; expense: number[] }` |
| `frontend/src/api/reports.ts` | 新增 `expenseYearly(year)` |
| `frontend/src/components/YearlyChart.vue` | **新建**：12 月双柱图（样式复用 MonthlyChart：渐变柱、tooltip、w 单位 y 轴） |
| `frontend/src/views/Dashboard.vue` | 卡片标题「年度收支」；月份选择器 → `type="year"` 年份选择器；`loadMonthly` → `loadYearly`（`reportsApi.expenseYearly`） |

### 3.3 非目标（YAGNI）

- `MonthlyChart.vue` 不改（收支页仍用单月图）
- 后端 trend 接口不动（Reports 页在用）
- 不做滚动年度、不做同比/环比

## 4. 验收标准

1. **后端**：curl 新接口返回 12 个月数组；无数据月份补 0；year 缺省为当前年；非法 year 不报错（返回空 12 月）。
2. **前端**：仪表盘显示 12 根柱（收入/支出各 12 根）；年份选择器切换触发重新加载；月度卡片标题正确。
3. **回归**：`npm run build` 零错误；收支页/报表页图表不受影响；后端 103 项集成测试通过（含新增 yearly 用例）。
