# 收支月度与报表隔离修复设计

## 目标
修复收支月度聚合和报表查询跨用户、跨账本泄露问题，使查询范围由当前认证用户和当前活动账本共同决定。

## 现状与约束
- 请求已通过 JWT 认证，且大多数业务路由可通过 `ctx_ledger_id()` 校验账本成员关系。
- `daily_expenses`、`categories`、`assets`、`transactions` 已有可空 `ledger_id` 列，但部分写路径尚未填充它。
- 现有日常收支月度 repository 函数显式丢弃 `user_id`；报表 service 的部分查询只有 `user_id`，没有 `ledger_id`。
- 不能通过简单加入 `ledger_id=?` 让历史 `ledger_id IS NULL` 数据全部消失，因此必须先确认当前写入/迁移策略，再选择安全的回填或兼容边界。

## 方案
1. 报表 controller/service 获取并校验 `ctx_ledger_id(c, user_id, "viewer")`。
2. 月度收支 usecase/repository 增加 `ledger_id` 参数；所有 totals/category/tag/daily SQL 同时限定 `user_id` 和 `ledger_id`。
3. expense report service 的 monthly/trend/yearly/category/tag 查询全部限定当前用户和账本；JOIN 的 categories/tags 也增加账本/用户边界。
4. 对仍未填充 `ledger_id` 的旧记录，不使用无条件 `IS NULL` 兜底；通过迁移/写入路径确定归属后再回填，无法安全判断归属的记录不得被纳入任意账本。
5. 增加跨用户和跨账本 API/仓储回归测试，覆盖月度总额、分类、标签、每日明细、趋势、年度、分类报表和标签报表。

## 测试策略
- 先写失败测试：同一用户两个账本、两个用户各一个账本，插入相同月份但不同金额/分类/标签的数据。
- 断言每个查询只返回当前 `user_id + ledger_id` 范围。
- 保留现有全量测试，补充旧数据/未归属数据不被错误归入活动账本的测试。
