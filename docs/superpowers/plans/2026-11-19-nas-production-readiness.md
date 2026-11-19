# NAS 长期生产就绪与灾备增强实施计划 (全栈闭环增强型)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 针对个人 NAS 长期 7x24h 生产运行场景，实现调度引擎动态休眠（支持机械硬盘休眠）、SIGTERM 优雅停机与断电防损（WAL Checkpoint）、官方 sqlite3_backup 在线热备份与自动轮转、全链路出站网络连接超时加固以及前端数据与灾备中心管理看板。

**Architecture:** 按照 DDD 四层分层与 csilk 异步架构，在核心层重构调度引擎以事件与时间轴驱动休眠；在基础设施层封装 SQLite 分步低冲击热备份引擎与原子写；在接口层捕获 POSIX 信号与暴露防穿越备份 API；全量加固出站 libcurl 连接超时；前端在系统设置新增灾备看板。

**Tech Stack:** C23, SQLite 3.35+, csilk (libuv + HTTP engine), libcurl, Vue 3, TypeScript, Pinia, Element Plus.

**Spec:** [docs/superpowers/specs/2026-11-19-nas-production-readiness-design.md](file:///data/home/quintin/workspace/source/c/Minefolio/docs/superpowers/specs/2026-11-19-nas-production-readiness-design.md)

## Global Constraints

- 所有构建与测试必须在受限内存环境（`./backend/tests/run_isolated.sh`，上限 2G/4G/6G）下运行，双引擎（SQLite/PostgreSQL）通过。
- C 代码保持 `-Werror` 0 警告，严格遵循 DDD 分层（控制器禁止直接编写内联 SQL）。
- 热备份过程严禁阻塞前台任何业务事务（利用 `sqlite3_backup_step(100)` 分片拷贝与微睡眠出让锁）。
- 备份文件名必须严格白名单校验（`^[a-zA-Z0-9_\-\.]+\.db$`），严防目录穿越（`../`）。
- 前端 `vue-tsc -b` 与 `vite build` 必须 0 错误。

## Review Focus

- **RF-01 (路径穿越注入)**: 下载或删除传入 `../../etc/passwd` 或 `..%2F..%2Fminefolio.db`，系统必须严格拦截返回错误，不得触碰目标目录之外的任何文件。（在 Task 3/4 验证）
- **RF-02 (热备份期间并发写入)**: 在热备份执行分片拷贝期间，并发写入多笔账单并更新资产，系统不应发生死锁，账单正常写入，备份生成的 `.db` 具备完整一致性。（在 Task 3 验证）
- **RF-03 (备份目录缺省与权限)**: `data/backups/` 目录初次不存在或路径无权限时，系统必须自动安全创建或妥善返回错误码，不得产生空指针解引用崩溃。（在 Task 3 验证）
- **RF-04 (出站网络黑洞挂起)**: 模拟外部服务连接丢包，libcurl 必须在 ≤ 5s 内以 `CURLE_OPERATION_TIMEDOUT` 返回，不得无限制阻塞线程。（在 Task 5 验证）
- **RF-05 (SIGTERM 信号优雅刷盘)**: Docker 发送 `SIGTERM` 信号停机，服务必须排空当前连接、执行 `PRAGMA wal_checkpoint(TRUNCATE)`，且退出码为 0。（在 Task 2 验证）

---

### Task 1: 调度引擎动态休眠与条件变量快速唤醒

**Files:**
- Modify: `backend/src/core/scheduler/scheduler_engine.c:580-670`
- Test: `backend/tests/unit/test_scheduler_idle_sleep.c`
- Modify: `backend/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `csilk_db_query_json()`, `cron_format_next_str()`, `pthread_cond_timedwait()`
- Produces: `scheduler_engine_thread()` 动态休眠逻辑，`scheduler_engine_notify_reload()` 毫秒唤醒

- [ ] **Step 1: 编写调度引擎动态休眠的失败单元测试**
  创建 `backend/tests/unit/test_scheduler_idle_sleep.c`：
  - 测试 1：在无待执行任务时，验证调度线程计算出的休眠间隔能够达到 60 秒上限，不会持续每秒查询数据库；
  - 测试 2：验证在调度线程处于休眠状态时，调用 `scheduler_engine_notify_reload()` 能在 50ms 内立刻唤醒线程并重置状态。
  在 `backend/tests/CMakeLists.txt` 中注册该测试。

- [ ] **Step 2: 运行测试验证失败**
  运行：`./backend/tests/run_isolated.sh build backend/build && ./backend/build/test_scheduler_idle_sleep`
  预期：FAIL（休眠或唤醒逻辑尚未按动态时间轴改造）

- [ ] **Step 3: 重构 `scheduler_engine.c` 调度主循环**
  - 在遍历 `scheduled_jobs` 任务时，记录活跃任务中最早的未来运行时间 `min_next_sec`；
  - 计算 `time_t sleep_sec = (min_next_sec > now) ? ((min_next_sec - now < 60) ? (min_next_sec - now) : 60) : 1;`；
  - 若无未来任务，默认 `sleep_sec = 60`；
  - 使用 `clock_gettime(CLOCK_REALTIME, &ts)` 将 `ts.tv_sec += sleep_sec` 传入 `pthread_cond_timedwait(&s_engine_cond, &s_engine_lock, &ts)`；
  - 确保 `s_reload_requested` 收到通知时立即重置并重新扫描。

- [ ] **Step 4: 运行测试验证通过**
  运行：`./backend/tests/run_isolated.sh build backend/build && ./backend/build/test_scheduler_idle_sleep`
  预期：PASS

- [ ] **Step 5: 提交代码**
  ```bash
  git add backend/src/core/scheduler/scheduler_engine.c backend/tests/unit/test_scheduler_idle_sleep.c backend/tests/CMakeLists.txt
  git commit -m "perf(scheduler): implement dynamic sleep and condition variable wakeup for HDD spindown"
  ```

---

### Task 2: SIGTERM 优雅停机与 WAL Checkpoint

**Files:**
- Modify: `backend/src/common/db.h`
- Modify: `backend/src/common/db.c`
- Modify: `backend/src/main.c:50-70, 180-192`
- Test: `backend/tests/unit/test_wal_checkpoint.c`
- Modify: `backend/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `csilk_server_stop()`, `csilk_app_server()`, `csilk_db_exec()`
- Produces: `int db_wal_checkpoint(csilk_db_pool_t* pool);`，`handle_shutdown_signal(int sig)`

- [ ] **Step 1: 编写 WAL Checkpoint 单元测试**
  创建 `backend/tests/unit/test_wal_checkpoint.c`：
  - 测试在 SQLite WAL 模式下执行 `db_wal_checkpoint(pool)`，断言返回 0，且数据库 WAL 文件被成功截断。
  在 `backend/tests/CMakeLists.txt` 中注册该测试。

- [ ] **Step 2: 运行测试验证失败**
  运行：`./backend/tests/run_isolated.sh build backend/build && ./backend/build/test_wal_checkpoint`
  预期：FAIL（`db_wal_checkpoint` 尚未声明与实现）

- [ ] **Step 3: 实现 `db_wal_checkpoint` 与 `main.c` 信号监听**
  - 在 `common/db.h` 声明并在 `common/db.c` 实现 `int db_wal_checkpoint(csilk_db_pool_t* pool)`：
    - 若 `!g_is_postgres`，执行 `csilk_db_exec(pool, "PRAGMA wal_checkpoint(TRUNCATE);")`；
  - 在 `main.c` 引入 `static csilk_app_t* g_app = NULL;`；
  - 编写 `static void handle_shutdown_signal(int sig)`，捕获 `SIGTERM` 与 `SIGINT`，内部通过 `csilk_app_server(g_app)` 调用 `csilk_server_stop()`；
  - 在 `main.c` 的 `csilk_app_run` 退出之后，依序执行停机逻辑，并在 `csilk_db_pool_free(pool)` 前调用 `db_wal_checkpoint(pool)`。

- [ ] **Step 4: 运行测试验证通过**
  运行：`./backend/tests/run_isolated.sh build backend/build && ./backend/build/test_wal_checkpoint`
  预期：PASS

- [ ] **Step 5: 提交代码**
  ```bash
  git add backend/src/common/db.h backend/src/common/db.c backend/src/main.c backend/tests/unit/test_wal_checkpoint.c backend/tests/CMakeLists.txt
  git commit -m "feat(server): handle SIGTERM gracefully and execute WAL truncate checkpoint on shutdown"
  ```

---

### Task 3: SQLite 官方在线热备份引擎与原子写

**Files:**
- Modify: `deps/csilk/include/csilk/drivers/db.h`
- Modify: `deps/csilk/src/drivers/db/sqlite.c`
- Create: `backend/src/infrastructure/database/db_backup.h`
- Create: `backend/src/infrastructure/database/db_backup.c`
- Test: `backend/tests/unit/test_db_backup.c`
- Modify: `backend/CMakeLists.txt`
- Modify: `backend/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `csilk_db_pool_t`, `sqlite3_backup_init()`, `sqlite3_backup_step()`, `statvfs()`
- Produces:
  - `sqlite3* csilk_db_pool_get_sqlite3(csilk_db_pool_t* pool);`
  - `bool db_backup_is_valid_filename(const char* filename);`
  - `int db_backup_create(csilk_db_pool_t* pool, const char* backup_dir, char* out_filename, size_t out_filename_size);`
  - `int db_backup_get_status(csilk_db_pool_t* pool, const char* backup_dir, csilk_json_t** out_status);`
  - `int db_backup_rotate(const char* backup_dir, int max_keep);`
  - `int db_backup_delete(const char* backup_dir, const char* filename);`

- [ ] **Step 1: 编写热备份引擎单元测试 (覆盖 RF-01, RF-02, RF-03)**
  创建 `backend/tests/unit/test_db_backup.c`：
  - 测试 1 (RF-01)：测试 `db_backup_is_valid_filename`，传入 `../../etc/passwd`、`../secret.db`、`null\0.db`、`test` 断言全返回 false；仅合规 `minefolio_backup_*.db` 返回 true；
  - 测试 2 (RF-03)：测试在 `data/backups/` 不存在时调用 `db_backup_create`，能自动创建目录并生成备份；
  - 测试 3 (RF-02)：测试备份完成后打开生成的备份文件，执行 `PRAGMA integrity_check` 验证数据完整无损；
  - 测试 4：测试 `db_backup_rotate` 超过保留上限时按 mtime 升序淘汰旧备份；
  - 测试 5：测试 `db_backup_get_status` 返回的 `db_size`、`wal_size`、`disk_free_bytes` 字段合理性。
  在 `backend/tests/CMakeLists.txt` 注册该测试。

- [ ] **Step 2: 运行测试验证失败**
  运行：`./backend/tests/run_isolated.sh build backend/build && ./backend/build/test_db_backup`
  预期：FAIL（头文件与函数未实现）

- [ ] **Step 3: 在 csilk 中增加 `csilk_db_pool_get_sqlite3` 并实现 `db_backup.h/.c`**
  - 在 `deps/csilk/src/drivers/db/sqlite.c` 与 `deps/csilk/include/csilk/drivers/db.h` 增加 `csilk_db_pool_get_sqlite3(pool)`；
  - 创建 `backend/src/infrastructure/database/db_backup.h` 与 `backend/src/infrastructure/database/db_backup.c`：
    - 校验文件名白名单（`^[a-zA-Z0-9_\-\.]+\.db$`，长度 5~128）；
    - 备份至临时文件 `.db.tmp`，每 100 页分片拷贝并微休眠 `usleep(1000)`；
    - 成功后通过 `rename()` 实现原子落盘；
    - 自动轮转调用 `db_backup_rotate(backup_dir, 7)`；
    - 读取 `stat()` 与 `statvfs()` 汇总存储指标。
  - 在 `backend/CMakeLists.txt` 中将 `backend/src/infrastructure/database/db_backup.c` 加入编译。

- [ ] **Step 4: 运行测试验证通过**
  运行：`./backend/tests/run_isolated.sh build backend/build && ./backend/build/test_db_backup`
  预期：PASS

- [ ] **Step 5: 提交代码**
  ```bash
  git add deps/csilk/include/csilk/drivers/db.h deps/csilk/src/drivers/db/sqlite.c backend/src/infrastructure/database/db_backup.h backend/src/infrastructure/database/db_backup.c backend/CMakeLists.txt backend/tests/unit/test_db_backup.c backend/tests/CMakeLists.txt
  git commit -m "feat(database): implement online sqlite hot backup engine with atomic file rotation"
  ```

---

### Task 4: 系统管理备份控制接口与定时轮转任务

**Files:**
- Modify: `backend/src/interfaces/http/controllers/admin_controller.h`
- Modify: `backend/src/interfaces/http/controllers/admin_controller.c`
- Modify: `backend/src/core/scheduler/scheduler_engine.c:380-420`
- Test: `backend/tests/test_link.sh`

**Interfaces:**
- Consumes: `db_backup_create()`, `db_backup_get_status()`, `db_backup_delete()`, `respond_ok()`, `respond_error()`
- Produces:
  - `GET /api/system/backup/status`
  - `POST /api/system/backup/create`
  - `GET /api/system/backup/download/:filename`
  - `DELETE /api/system/backup/:filename`
  - 系统调度 handler `"db_auto_backup"`

- [ ] **Step 1: 在 `test_link.sh` 中增加 Case 41 备份管理 API 自动化测试**
  在 `backend/tests/test_link.sh` 增加 Case 41：
  - 调用 `GET /api/system/backup/status` 验证返回 200 与存储指标；
  - 调用 `POST /api/system/backup/create` 触发在线热备份，提取返回的 `filename`；
  - 调用 `GET /api/system/backup/download/:filename` 验证二进制下载及响应头；
  - 恶意调用 `GET /api/system/backup/download/../../etc/passwd` 验证返回 400 被拒绝；
  - 调用 `DELETE /api/system/backup/:filename` 验证文件删除成功；
  - 再次获取状态验证列表已清空。

- [ ] **Step 2: 运行测试验证失败**
  运行：`./backend/tests/test_link.sh`
  预期：FAIL（路由 404）

- [ ] **Step 3: 在 `admin_controller.c` 实现 REST 路由并在调度引擎注册内置任务**
  - 在 `admin_controller.c` 实现：
    - `system_backup_status`
    - `system_backup_create`
    - `system_backup_download`（利用 `csilk_app_static` 或读取流式返回，设置 `Content-Disposition: attachment` 与 `application/x-sqlite3`）
    - `system_backup_delete`
  - 在 `register_admin_routes` 中注册上述 4 条路由；
  - 在 `scheduler_engine.c` 的 `action_type == "system_handler"` 中处理 `"db_auto_backup"`，触发 `db_backup_create` 与 `db_backup_rotate`。

- [ ] **Step 4: 运行集成测试验证通过**
  运行：`./backend/tests/test_link.sh`
  预期：PASS（Case 41 所有断言全部通过）

- [ ] **Step 5: 提交代码**
  ```bash
  git add backend/src/interfaces/http/controllers/admin_controller.h backend/src/interfaces/http/controllers/admin_controller.c backend/src/core/scheduler/scheduler_engine.c backend/tests/test_link.sh
  git commit -m "feat(api): expose system backup management endpoints and wire scheduled backup task"
  ```

---

### Task 5: 全链路出站网络连接超时加固 (RF-04)

**Files:**
- Modify: `backend/src/application/ai/probe_usecase.c:270-285`
- Modify: `backend/src/application/auth/usecases.c:750-770, 800-815`
- Modify: `backend/src/services/ai/tools/web_search_tool.c:210-230`
- Modify: `backend/src/services/ai/tools/mcp/mcp_client.c:200-220`
- Modify: `backend/src/services/market/quote_engine.c:75-90`
- Modify: `backend/src/interfaces/http/controllers/receipt_controller.c:410-425`

**Interfaces:**
- Consumes: `CURLOPT_CONNECTTIMEOUT`, `CURLOPT_CONNECTTIMEOUT_MS`
- Produces: 5s 强制出站 TCP SYN 连接超时保护

- [ ] **Step 1: 扫描并编写连接超时防护校验规则**
  检查所有 `curl_easy_setopt` 涉及 `CURLOPT_TIMEOUT` 的调用点，确保每个点均配对存在 `CURLOPT_CONNECTTIMEOUT`（或 `CURLOPT_CONNECTTIMEOUT_MS`）。

- [ ] **Step 2: 注入连接超时配置**
  - 在 `probe_usecase.c` 添加：`curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);`
  - 在 `auth/usecases.c` 两个 OAuth 请求添加：`curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);`
  - 在 `web_search_tool.c` 添加：`curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 4000L);`
  - 在 `mcp_client.c` 添加：`curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 4000L);`
  - 在 `quote_engine.c` 添加：`curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);`
  - 在 `receipt_controller.c` 添加：`curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);`

- [ ] **Step 3: 运行已有单元测试与集成测试**
  运行：`./backend/tests/run_isolated.sh build backend/build && ctest --test-dir backend/build --output-on-failure`
  预期：PASS

- [ ] **Step 4: 提交代码**
  ```bash
  git add backend/src/application/ai/probe_usecase.c backend/src/application/auth/usecases.c backend/src/services/ai/tools/web_search_tool.c backend/src/services/ai/tools/mcp/mcp_client.c backend/src/services/market/quote_engine.c backend/src/interfaces/http/controllers/receipt_controller.c
  git commit -m "fix(network): harden all outbound http requests with strict connect timeouts"
  ```

---

### Task 6: 前端数据与灾备中心管理看板

**Files:**
- Create: `frontend/src/api/backup.ts`
- Create: `frontend/src/components/settings/DatabaseBackup.vue`
- Modify: `frontend/src/views/Settings.vue`
- Modify: `frontend/src/locales/zh-CN.json`
- Modify: `frontend/src/locales/en-US.json`

**Interfaces:**
- Consumes: `/api/system/backup/*` REST API
- Produces: 前端灾备看板组件，包含指标卡片、热备份触发、备份历史列表、安全下载与删除

- [ ] **Step 1: 创建前端 API 模块与多语言翻译**
  - 创建 `frontend/src/api/backup.ts`，导出 `getBackupStatus()`, `createBackup()`, `downloadBackup(filename)`, `deleteBackup(filename)`；
  - 在 `zh-CN.json` 与 `en-US.json` 中增加 `settings.tabBackup`, `settings.backupStatus`, `settings.dbSize`, `settings.walSize`, `settings.diskFree`, `settings.createBackup`, `settings.backupHistory`, `settings.download`, `settings.deleteBackupConfirm` 等词条。

- [ ] **Step 2: 创建 `DatabaseBackup.vue` 组件**
  - Bento 风格指标卡片展示数据库引擎、主库大小、WAL 大小、可用磁盘空间；
  - 操作区域包含「立即创建热备份」按钮与机制说明（说明 SQLite 官方在线分片热备份，不影响业务读写）；
  - 表格展示历史备份：文件名、文件大小、创建时间、操作（下载到本地、删除）；
  - 下载使用 `URL.createObjectURL(blob)` 配合虚拟 `<a>` 标签触发浏览器下载。

- [ ] **Step 3: 挂载到 `Settings.vue`**
  - 在 `frontend/src/views/Settings.vue` 的 `el-tabs` 中增加 `name="backup"` 的 `el-tab-pane`，挂载 `<DatabaseBackup />`。

- [ ] **Step 4: 前端编译与类型检查**
  运行：`npm --prefix frontend run build`
  预期：0 errors, 成功输出构建产物至 `frontend/dist`。

- [ ] **Step 5: 提交代码**
  ```bash
  git add frontend/src/api/backup.ts frontend/src/components/settings/DatabaseBackup.vue frontend/src/views/Settings.vue frontend/src/locales/zh-CN.json frontend/src/locales/en-US.json
  git commit -m "feat(frontend): add disaster recovery and backup management dashboard in settings"
  ```

---

### Task 7: 隔离全量编译、ASan 零泄漏检漏与端到端回归验证

**Files:**
- None (Verification task)

**Interfaces:**
- Consumes: All modules from Tasks 1-6
- Produces: 最终生产就绪度验证证据链

- [ ] **Step 1: 内存隔离全量构建与单元测试**
  运行：`./backend/tests/run_isolated.sh build backend/build`
  运行：`./backend/tests/run_isolated.sh run backend/build/test_scheduler_idle_sleep`
  运行：`./backend/tests/run_isolated.sh run backend/build/test_wal_checkpoint`
  运行：`./backend/tests/run_isolated.sh run backend/build/test_db_backup`
  预期：全部 PASS

- [ ] **Step 2: 运行完整集成测试套件**
  运行：`./backend/tests/test_link.sh`
  预期：41 个测试用例、200+ 断言全部通过，Case 41 备份端到端测试绿灯。

- [ ] **Step 3: ASan / UBSan 内存检漏**
  运行：`./backend/tests/run_isolated.sh asan`
  预期：0 内存泄漏，0 缓冲区溢出。

- [ ] **Step 4: 前端完整构建验证**
  运行：`cmake --build backend/build --parallel && npm --prefix frontend run build`
  预期：全量编译 0 报错。
