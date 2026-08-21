# Findings & Decisions — Minefolio 代码分析

## Requirements
- 对 Minefolio 项目进行完整代码分析
- 识别已知 bug、安全隐患、代码质量问题
- 制定分批改进计划

## Research Findings

### 后端架构
- **分层**：Controller → Service → Repository，controller 层极薄（约 1 行委托）
- **路由注册**：`main.c` 中 9 个 `register_*_routes()` 函数，分散在各 service 文件末尾
- **事务保护**：所有 multi-step 操作（交易创建/更新/删除、资产更新）均包裹在 `BEGIN/COMMIT/ROLLBACK`
- **参数化查询**：所有用户输入使用 `?` 占位符，无字符串拼接注入风险
- **数字解析**：`db_get_num()` / `db_get_int()` 兼容 JSON 字符串/数字节点，避免 `csilk_json_get_number()` 返回 0 的陷阱

### 核心业务逻辑
- **余额引擎** `balance_apply_delta()`：负债资产（loan/credit_card/other_liability）自动反转 delta 方向
- **交易类型注册表** `tx_types.c`：9 种类型定义 `balance_dir`/`linked_dir`/`stat_dir`，集中管理方向语义
- **投资类特殊处理**：buy/sell 调用 `apply_position()` 更新 quantity/cost_basis/net_value，`current_value` 由余额联动独立维护
- **PnL 双轨制**：`total_cost_for_pnl`（不含 fee）vs `total_cost_basis`（含 fee），口径严格分离

### 前端架构
- **HTTP 封装**：`http.ts` 统一注入 JWT + CSRF，解包 `{code, message, data}` envelope
- **移动端**：`offline-http.ts` 实现离线请求队列，`sql-wasm-base64.ts` 内嵌 WASM
- **路由守卫**：`router.beforeEach` 异步检查系统初始化状态
- **滚动布局**：`.main` 为唯一滚动容器，页面组件 `overflow: hidden` 实现内部独立滚动

### 数据库 Schema
- **多租户**：单 `users` 表，所有查询通过 JWT 中的 `user_id` 过滤
- **审计日志**：`asset_balance_logs` 无资产外键（删资产后日志保留），`idx_balance_logs_asset` 索引
- **分类种子**：`category_seed_state` 表记录每个用户的种子状态（懒加载设计）
- **索引**：`daily_expenses` 有 date/type/category 三索引，`tags` 有 user_id 索引

## Technical Decisions

| Decision | Rationale |
|----------|-----------|
| 负债资产反转方向 | 净值 = 资产 - 负债，统一方向后汇总计算保持正确 |
| PnL 双轨成本口径 | `cost_basis` DB 列含 fee 用于展示；`avg_cost` 计算不含 fee 用于盈亏 |
| sql.js WASM base64 内嵌 | Capacitor WebView 无法可靠 fetch `capacitor://` 协议下的 wasm 文件 |
| 交易 fee 单独建 row | 保持 transactions 表语义完整，fee 行不参与 position 计算（qty=0） |
| asset_balance_logs 无外键 | 历史日志不因资产删除而消失，保留完整审计轨迹 |

## Issues Encountered

| Issue | Resolution |
|-------|------------|
| `assets_update()` 中 `nv_str` 栈空间复用风险 | AGENTS.md 已记录：必须将 `nv_str` 声明在 `upd_params[]` 之前 |
| fee 行 note 不能为空 | AGENTS.md 已记录：fallback 到字面量 `"fee"` 字符串 |
| JWT secret 硬编码默认值 | 开发环境可接受，生产环境须设置 `MINEFOLIO_JWT_SECRET` 环境变量 |
| `transactions_update()` 投资类回滚逻辑冗长 | 待重构：抽出 `reverse_position()` 辅助函数 |
| 密码校验长度不一致 | register ≥4，setup/change_password ≥6，待统一 |
| `report_asset_trend` 递归 CTE 性能 | 365 天场景下可能较慢，可考虑预生成日期维度表 |

## Resources
- csilk 框架: https://github.com/quintin-lee/csilk
- 关键文件:
  - `backend/src/common/balance.c` — 余额引擎核心
  - `backend/src/common/tx_types.c` — 交易类型注册表
  - `backend/src/services/transaction_service.c` — 最复杂的业务逻辑
  - `backend/src/services/report_service.c` — 报表聚合 + PnL 计算
  - `frontend/src/utils/http.ts` — 统一 HTTP 客户端
  - `frontend/src/stores/auth.ts` — 登录状态 + RSA 加密
  - `backend/sql/migration.sql` — 数据库 schema

## Visual/Browser Findings
- （本次为纯代码分析，无浏览器交互）

---
*Last updated: 2025-08-21*
