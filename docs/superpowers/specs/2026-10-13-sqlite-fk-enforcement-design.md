# SQLite 外键强制执行设计（优化四部曲之①）

- 日期：2026-10-13
- 背景：PG 全量回归曾暴露 `daily_expenses.asset_id` 非法值在两库行为分歧——PG 因 FK 强 enforcement 在 INSERT 即 500，SQLite 因池连接外键未生效而写穿、靠 `apply_balance` 报 1002。当时用应用层预校验临时统一了单点，但全仓其它表仍裸奔。
- 所属系列：DB 参数绑定加固 / **SQLite 外键生效（本篇）** / PG 回归进仓 / 性能优化。

## 目标

SQLite 下打开外键强制执行，与 PG 行为对齐；`test_link.sh` 147 项修到全绿。

非目标：切换数据库驱动、改动 PG 路径、性能优化。

## 关键约束

- csilk 是预编译库（0.5.3，无源码），连接池经 `csilk_db_pool_new(driver, dsn)` 一句话创建，**无连接 checkout 钩子**。
- `PRAGMA foreign_keys` 按连接生效。若 pool 是多连接/懒创建，单次执行可能只覆盖部分连接——必须实证，不能假设。
- 自研 mf adapter（`sqlite_adapter.c:49`）的 open 里虽已有该 pragma，但应用层未使用；切换面巨大，本次不动。

## 方案（已选 A）

- **A（采用）：初始化后补 pragma，回归修到全绿。** `db_init`（`backend/src/common/db.c:78`）中 pool 建好后执行 `csilk_db_exec(pool, "PRAGMA foreign_keys=ON")`，然后跑全量 SQLite 回归，冒出一个违反点修一个。
- B（备选/长期）：切到自研 mf adapter，本次只记录不做。

## 改动

- 仅 `backend/src/common/db.c` 的 `db_init` 加一行 pragma 执行。无新文件、无新依赖。

## 验证

1. 负向用例：非法 asset 的 expense INSERT 在 SQLite 下必须失败（连跑 3 遍，防连接间不一致）。
2. `test_link.sh` 全量绿（147 项）。
3. PG 套件顺手跑一遍确认不受影响。

## 止损线

- 若负向验证时好时坏（pragma 粘不住），立刻停手、不硬 hack，转方案 B 评估。
- 若违反点超过 5 处且涉及业务语义（如删除顺序依赖），停下来重新对齐，不硬修。
