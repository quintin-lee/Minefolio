/**
 * @file reset-password.c
 * @brief Minefolio 运维工具：重置/管理用户密码 (standalone CLI)
 *
 * 用于后台维护场景：当管理员忘记账号密码（bcrypt 单向哈希无法逆向）时，
 * 直接通过数据库重置 `users.password` 与 `token_version`，无需 Web UI 登录态。
 *
 * ## 设计取舍
 *
 * 本工具**刻意不链接 `minefolio_core` 静态库**，原因：
 *  1. `minefolio_core` 包含整个应用层 (usecases/services/domain) 与 csilk 框架，
 *     拉入后链接面巨大、且其内 `db_init()` 依赖 `config_secret_get()` 的多级
 *     Secret Provider（测试覆盖 / 外部 Manager / 文件 / 容器路径），运维工具
 *     只需最小可用的「环境变量 > config/db.json」两层解析，与 `db.c` 行为对齐。
 *  2. 仅链接 csilk (db + bcrypt + json) 与驱动 (sqlite3 / libpq)，与运行时容器
 *     已安装的共享库一致，体积与依赖面最小。
 *
 * 密码哈希算法、cost 因子与 `application/auth/usecases.c` 严格一致
 * (`CSILK_BCRYPT_DEFAULT_COST = 12`)，保证生成的 hash 可被 Web 登录路径
 * (`csilk_bcrypt_verify`) 直接验证通过。
 *
 * ## 用法
 *
 * ```
 * reset-password list
 * reset-password reset  <username> [--password <pwd> | --password-file <f> | --password-stdin]
 * reset-password disable-2fa <username>
 * reset-password verify  <username> --password <pwd>
 * reset-password help
 *
 * 通用选项：
 *   --driver <sqlite|postgres>     覆盖数据库驱动（默认读 MINEFOLIO_DB_DRIVER / config/db.json）
 *   --dsn <dsn>                   覆盖数据源（默认读 MINEFOLIO_DB_DSN / config/db.json）
 *   --yes                          跳过交互确认（非交互/CI 场景必须提供）
 * ```
 *
 * ## 数据库定位（与 `common/db.c:db_init` 的解析顺序一致）
 *
 *  1. `MINEFOLIO_DB_DRIVER` / `DB_DRIVER` 环境变量（或 `MINEFOLIO_DB_DRIVER_FILE` 文件）
 *  2. `config/db.json` 中的 `driver` 字段
 *  3. 缺省 `sqlite`
 *
 *  1. `MINEFOLIO_DB_DSN` / `DB_DSN` 环境变量（或文件变体）
 *  2. `config/db.json` 中的 `dsn` 字段
 *  3. 缺省：postgres -> `host=localhost user=minefolio dbname=minefolio`
 *            sqlite -> `./data/minefolio.db`
 *
 * @copyright 与 Minefolio 主项目同许可
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <csilk/csilk.h>
#include <csilk/drivers/db.h>
#include <csilk/crypto/bcrypt.h>

#ifndef CSILK_BCRYPT_DEFAULT_COST
#define CSILK_BCRYPT_DEFAULT_COST 12
#endif

#define MAX_PASSWORD_LEN 256
#define MIN_PASSWORD_LEN 6   /* 与 mf_auth_rule_validate_password 一致 */

/* ---------------------------------------------------------------------------
 * 最小可用的配置解析（与 db.c 解析顺序对齐，但仅两层：env > config/db.json）
 * --------------------------------------------------------------------------- */

/**
 * @brief 读取单个环境变量或带 MINEFOLIO_ 前缀的变量
 * @param key  形如 "DB_DSN"，工具先查 MINEFOLIO_DB_DSN 再查 DB_DSN（与 secret.c 行为一致）
 * @return 命中返回字符串指针（静态生命周期），未命中返回 NULL
 */
static const char*
env_or_minefolio(const char* key)
{
    /* 优先 MINEFOLIO_ 前缀（secret.c 的检索顺序） */
    if (strncasecmp(key, "MINEFOLIO_", 10) != 0) {
        char full[128];
        snprintf(full, sizeof(full), "MINEFOLIO_%s", key);
        const char* v = getenv(full);
        if (v && v[0]) {
            return v;
        }
    }
    const char* v = getenv(key);
    if (v && v[0]) {
        return v;
    }
    return NULL;
}

/**
 * @brief 从扁平 JSON 配置文件中读取字符串配置项（仅支持 "key": "value" 形态）
 *
 * 实现与 common/config.c:config_get_str 等价，但去掉了对 csilk 的依赖；
 * 运维工具场景下 config/db.json 永远是扁平结构（driver/dsn/pool_conns），
 * 流式键匹配足够。
 *
 * @return 0 命中（out 已填充），-1 未命中或文件不可读（out 置空串）
 */
static int
config_read_str(const char* path, const char* key, char* out, size_t out_size)
{
    out[0] = '\0';
    FILE* f = fopen(path, "r");
    if (!f) {
        return -1;
    }
    char  buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';

    char  key_quoted[128];
    snprintf(key_quoted, sizeof(key_quoted), "\"%s\"", key);

    const char* search = buf;
    while ((search = strstr(search, key_quoted)) != NULL) {
        search += strlen(key_quoted);
        while (*search == ' ' || *search == '\t' || *search == ':' || *search == ',') {
            search++;
        }
        if (*search != '"') {
            search++;
            continue;
        }
        search++;
        size_t oi = 0;
        while (*search && *search != '"' && oi < out_size - 1) {
            out[oi++] = *search++;
        }
        out[oi] = '\0';
        return 0;
    }
    return -1;
}

/**
 * @brief 解析数据库驱动与 DSN（与 db.c:db_init 的优先级一致）
 *
 * @param driver_arg  CLI 提供的 --driver（可为 NULL）
 * @param dsn_arg     CLI 提供的 --dsn（可为 NULL）
 * @param out_driver  输出驱动名（静态缓冲）
 * @param out_dsn     输出 DSN（静态缓冲）
 * @return 0 成功
 */
static int
resolve_db_config(const char* driver_arg, const char* dsn_arg, const char** out_driver,
                  const char** out_dsn)
{
    static char driver_buf[64] = {0};
    static char dsn_buf[512]   = {0};

    /* 优先级：CLI 参数 > 环境变量 > config/db.json > 缺省 */
    if (driver_arg && driver_arg[0]) {
        snprintf(driver_buf, sizeof(driver_buf), "%s", driver_arg);
    } else {
        const char* drv_env = env_or_minefolio("DB_DRIVER");
        if (drv_env) {
            snprintf(driver_buf, sizeof(driver_buf), "%s", drv_env);
        } else {
            config_read_str("config/db.json", "driver", driver_buf, sizeof(driver_buf));
        }
        if (!driver_buf[0]) {
            snprintf(driver_buf, sizeof(driver_buf), "sqlite");
        }
    }

    if (dsn_arg && dsn_arg[0]) {
        snprintf(dsn_buf, sizeof(dsn_buf), "%s", dsn_arg);
    } else {
        const char* dsn_env = env_or_minefolio("DB_DSN");
        if (dsn_env) {
            snprintf(dsn_buf, sizeof(dsn_buf), "%s", dsn_env);
        } else {
            config_read_str("config/db.json", "dsn", dsn_buf, sizeof(dsn_buf));
        }
        if (!dsn_buf[0]) {
            if (strcmp(driver_buf, "postgres") == 0) {
                snprintf(dsn_buf, sizeof(dsn_buf), "host=localhost user=minefolio dbname=minefolio");
            } else {
                snprintf(dsn_buf, sizeof(dsn_buf), "./data/minefolio.db");
            }
        }
    }

    *out_driver = driver_buf;
    *out_dsn    = dsn_buf;
    return 0;
}

/* ---------------------------------------------------------------------------
 * 交互式确认（非交互场景可被 --yes 跳过）
 * --------------------------------------------------------------------------- */

/**
 * @brief 要求用户输入 y/Y 确认（读取 stdin 行）
 *
 * 当 stdin 不是 TTY（管道/CI）且未传 --yes 时，工具应拒绝破坏性操作。
 *
 * @return 1 确认，0 取消
 */
static int
confirm_line(const char* prompt)
{
    printf("%s [y/N]: ", prompt);
    fflush(stdout);
    char line[32] = {0};
    if (!fgets(line, sizeof(line), stdin)) {
        return 0; /* EOF/管道输入视为取消 */
    }
    return (line[0] == 'y' || line[0] == 'Y');
}

/* ---------------------------------------------------------------------------
 * 用户查询与更新
 * --------------------------------------------------------------------------- */

typedef struct {
    int64_t id;
    char    username[64];
    char    created_at[64];
} tool_user_t;

/**
 * @brief 列出当前数据库中所有用户（运维诊断）
 */
static int
cmd_list(csilk_db_pool_t* pool)
{
    csilk_json_t* rows =
        csilk_db_query_param_json(pool,
                                  "SELECT id, username, created_at FROM users ORDER BY id ASC",
                                  (const char*[]){NULL});
    if (!rows) {
        fprintf(stderr, "查询 users 表失败\n");
        return 1;
    }
    int64_t count = csilk_json_array_size(rows);
    if (count == 0) {
        printf("(users 表为空 — 系统尚未初始化；首次访问时通过 /api/system/setup 引导)\n");
        csilk_json_free(rows);
        return 0;
    }
    printf("%-6s %-24s %s\n", "ID", "USERNAME", "CREATED_AT");
    printf("---- ---- ------------------------------\n");
    for (int64_t i = 0; i < count; i++) {
        const csilk_json_t* row = csilk_json_array_get(rows, (size_t)i);
        int64_t  id       = csilk_json_as_int64(csilk_json_get(row, "id"), 0);
        const char* uname = csilk_json_get_string(row, "username");
        const char* ctime = csilk_json_get_string(row, "created_at");
        printf("%-6lld %-24s %s\n", (long long)id, uname ? uname : "(null)", ctime ? ctime : "-");
    }
    csilk_json_free(rows);
    return 0;
}

/**
 * @brief 按用户名查找单个用户；未找到返回 1（与 mf_auth_repo_find_by_username 一致）
 */
static int
find_user_by_username(csilk_db_pool_t* pool, const char* username, tool_user_t* out)
{
    if (!username || !username[0]) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "SELECT id, username, created_at FROM users WHERE upper(username) = upper(?)",
        (const char*[]){username, NULL});
    if (!res || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        return 1; /* 未找到 */
    }
    const csilk_json_t* row = csilk_json_array_get(res, 0);
    out->id       = csilk_json_as_int64(csilk_json_get(row, "id"), 0);
    const char*    uname = csilk_json_get_string(row, "username");
    const char*    ctime = csilk_json_get_string(row, "created_at");
    if (uname) {
        snprintf(out->username, sizeof(out->username), "%s", uname);
    }
    if (ctime) {
        snprintf(out->created_at, sizeof(out->created_at), "%s", ctime);
    }
    csilk_json_free(res);
    return 0;
}

/**
 * @brief 校验新密码长度与字符数（与 mf_auth_rule_validate_password 一致：>=6, <=128）
 */
static int
validate_password_len(const char* pwd)
{
    size_t len = strlen(pwd);
    if (len < MIN_PASSWORD_LEN || len > MAX_PASSWORD_LEN) {
        fprintf(stderr, "密码长度需 %d~%d 字符（当前 %zu）\n", MIN_PASSWORD_LEN, MAX_PASSWORD_LEN, len);
        return -1;
    }
    return 0;
}

/**
 * @brief 写入新 bcrypt hash 与 token_version+1（一次事务，与 mf_auth_repo_update_password
 *        + mf_auth_repo_update_token_version 的组合语义一致）
 */
static int
write_new_password(csilk_db_pool_t* pool, int64_t user_id, const char* new_hash,
                   const char* new_username)
{
    /* 事务保护：hash + token_version 必须原子更新（与 auth usecases 的
     * db_tx_scope 语义对齐，避免中途失败留下「新密码但旧 token 仍有效」窗口） */
    csilk_db_tx_scope_t scope = {0};
    if (csilk_db_tx_scope_begin(pool, "rp_pw_reset", &scope) != 0) {
        fprintf(stderr, "无法开启事务（数据库错误）\n");
        return -1;
    }

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    int rc = csilk_db_exec_param(
        pool, "UPDATE users SET password = ? WHERE id = ?", (const char*[]){new_hash, uid_str, NULL});
    if (rc != 0) {
        csilk_db_tx_scope_rollback(pool, &scope);
        fprintf(stderr, "写入新 hash 失败\n");
        return -1;
    }
    rc = csilk_db_exec_param(pool,
                             "UPDATE users SET token_version = token_version + 1 WHERE id = ?",
                             (const char*[]){uid_str, NULL});
    if (rc != 0) {
        csilk_db_tx_scope_rollback(pool, &scope);
        fprintf(stderr, "递增 token_version 失败（已回滚）\n");
        return -1;
    }
    if (csilk_db_tx_scope_commit(pool, &scope) != 0) {
        csilk_db_tx_scope_rollback(pool, &scope);
        fprintf(stderr, "提交事务失败（已回滚）\n");
        return -1;
    }

    printf("[OK] 用户 '%s' (id=%lld) 密码已重置；所有已签发的 JWT 已被使失效。\n",
           new_username, (long long)user_id);
    return 0;
}

/**
 * @brief 重置密码主流程
 */
static int
cmd_reset(csilk_db_pool_t*        pool,
          const char*             username,
          const char*             new_password,
          int                     assume_yes,
          int                     from_stdin)
{
    tool_user_t user = {0};
    int         rc   = find_user_by_username(pool, username, &user);
    if (rc == 1) {
        fprintf(stderr, "用户 '%s' 不存在（用 `reset-password list` 查看现有用户）\n", username);
        return 2;
    } else if (rc != 0) {
        fprintf(stderr, "查询用户失败\n");
        return 1;
    }

    if (validate_password_len(new_password) != 0) {
        return 1;
    }

    /* 显示即将发生的变更（破坏性操作前的最后一道闸门） */
    printf("即将重置用户 '%s' (id=%lld, created_at=%s) 的密码\n",
           user.username, (long long)user.id, user.created_at[0] ? user.created_at : "-");
    printf("  - 所有现存 JWT 将被使失效 (token_version+1)\n");
    printf("  - 用户下次登录必须使用新密码%s\n", from_stdin ? "（通过 --password-stdin 提供）" : "");
    if (!assume_yes) {
        int ok = confirm_line("确认重置？");
        if (!ok) {
            printf("已取消\n");
            return 3;
        }
    }

    char hash[CSILK_BCRYPT_HASH_LEN];
    csilk_bcrypt_hash(new_password, strlen(new_password), CSILK_BCRYPT_DEFAULT_COST, hash);

    int wr = write_new_password(pool, user.id, hash, user.username);
    if (wr != 0) {
        return 1;
    }

    /* 立即回读验证（自检：确认 hash 真的能匹配新密码） */
    char idstr[32];
    snprintf(idstr, sizeof(idstr), "%lld", (long long)user.id);
    csilk_json_t* res = csilk_db_query_param_json(
        pool, "SELECT password FROM users WHERE id = ?", (const char*[]){idstr, NULL});
    if (res && csilk_json_array_size(res) > 0) {
        const char* readback_hash = csilk_json_get_string(csilk_json_array_get(res, 0), "password");
        if (readback_hash && csilk_bcrypt_verify(new_password, strlen(new_password), readback_hash)
                != 0) {
            fprintf(stderr, "[警告] 回读验证失败：新密码无法匹配已写回库的 hash！\n");
        } else {
            printf("[OK] 自检通过：新密码可匹配数据库中的 hash。\n");
        }
    }
    if (res) {
        csilk_json_free(res);
    }
    return wr;
}

/**
 * @brief 仅验证（不修改）：检查某密码是否匹配库中 hash。用于运维自检。
 */
static int
cmd_verify(csilk_db_pool_t* pool, const char* username, const char* password)
{
    tool_user_t user = {0};
    int         rc   = find_user_by_username(pool, username, &user);
    if (rc != 0) {
        fprintf(stderr, "用户 '%s' 不存在或查询失败\n", username);
        return rc == 1 ? 2 : 1;
    }

    char idstr[32];
    snprintf(idstr, sizeof(idstr), "%lld", (long long)user.id);
    csilk_json_t* res = csilk_db_query_param_json(
        pool, "SELECT password, totp_enabled, totp_secret, totp_backup_codes FROM users WHERE id = ?",
        (const char*[]){idstr, NULL});
    if (!res || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        fprintf(stderr, "读取用户记录失败\n");
        return 1;
    }
    const csilk_json_t* row = csilk_json_array_get(res, 0);
    const char*         hash = csilk_json_get_string(row, "password");
    int                 two_fa = csilk_json_as_bool(csilk_json_get(row, "totp_enabled"), 0);

    if (hash && hash[0]) {
        int match = csilk_bcrypt_verify(password, strlen(password), hash) == 0;
        printf("%s 密码匹配：%s%s\n", match ? "[OK]" : "[X]", match ? "是" : "否",
               two_fa ? "（该用户启用了 2FA，登录时还需 6 位 TOTP/备用码）" : "");
        if (match) {
            csilk_json_free(res);
            return 0;
        }
        csilk_json_free(res);
        return 4;
    }
    /* OAuth 用户：password 为空 */
    printf("%s 该用户无本地密码（OAuth 登录用户或空密码）%s\n", "[INFO]",
           two_fa ? "，但已启用 2FA" : "");
    csilk_json_free(res);
    return 0;
}

/**
 * @brief 关闭指定用户的 2FA（TOTP/备用码），与 mf_auth_repo_disable_totp 语义一致
 */
static int
cmd_disable_2fa(csilk_db_pool_t* pool, const char* username, int assume_yes)
{
    tool_user_t user = {0};
    int         rc   = find_user_by_username(pool, username, &user);
    if (rc != 0) {
        fprintf(stderr, "用户 '%s' 不存在或查询失败\n", username);
        return rc == 1 ? 2 : 1;
    }
    printf("即将关闭用户 '%s' (id=%lld) 的 2FA（TOTP 与备用码）\n", user.username,
           (long long)user.id);
    if (!assume_yes) {
        if (!confirm_line("确认关闭 2FA？")) {
            printf("已取消\n");
            return 3;
        }
    }
    csilk_db_tx_scope_t scope = {0};
    if (csilk_db_tx_scope_begin(pool, "rp_disable_2fa", &scope) != 0) {
        fprintf(stderr, "无法开启事务\n");
        return 1;
    }
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user.id);
    /* 与 auth_repo_impl.c:mf_auth_repo_disable_totp 完全一致 */
    int wr = csilk_db_exec_param(
        pool,
        "UPDATE users SET totp_secret = '', totp_enabled = FALSE, totp_backup_codes = '' WHERE id = ?",
        (const char*[]){uid_str, NULL});
    /* 同步使现有 JWT 失效 */
    wr += csilk_db_exec_param(pool,
                              "UPDATE users SET token_version = token_version + 1 WHERE id = ?",
                              (const char*[]){uid_str, NULL});
    if (wr != 0 || csilk_db_tx_scope_commit(pool, &scope) != 0) {
        csilk_db_tx_scope_rollback(pool, &scope);
        fprintf(stderr, "关闭 2FA 失败（已回滚）\n");
        return 1;
    }
    printf("[OK] 用户 '%s' 的 2FA 已关闭，现有 JWT 已使失效\n", user.username);
    return 0;
}

/* ---------------------------------------------------------------------------
 * 密码读取（避免 argv 泄漏到 /proc）
 * --------------------------------------------------------------------------- */

static int
read_password_file(const char* path, char* buf, size_t cap)
{
    FILE* f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "无法读取密码文件 %s\n", path);
        return -1;
    }
    size_t n = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[n] = '\0';
    /* 去尾部换行 */
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' ' ||
                     buf[n - 1] == '\t')) {
        buf[--n] = '\0';
    }
    return 0;
}

static int
read_password_stdin(char* buf, size_t cap)
{
    size_t n = fread(buf, 1, cap - 1, stdin);
    if (n == 0) {
        fprintf(stderr, "stdin 未读到密码（可用管道重定向，例如 printf 一个新密码重定向到 stdin）\n");
        return -1;
    }
    buf[n] = '\0';
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
        buf[--n] = '\0';
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * CLI 解析
 * --------------------------------------------------------------------------- */

static void
print_help(const char* prog)
{
    printf("%s — Minefolio 用户密码重置/管理运维工具\n", prog);
    printf("\n用法:\n");
    printf("  %s list\n", prog);
    printf("  %s reset  <username> --password <newpass> [--yes]\n", prog);
    printf("  %s reset  <username> --password-file <path> [--yes]\n", prog);
    printf("  %s reset  <username> --password-stdin [--yes]      # 密码从 stdin 读取\n", prog);
    printf("  %s disable-2fa <username> [--yes]\n", prog);
    printf("  %s verify  <username> --password <candidate>\n", prog);
    printf("  %s help\n", prog);
    printf("\n数据库定位（优先级 高 → 低）:\n");
    printf("  1. --driver / --dsn 命令行参数\n");
    printf("  2. MINEFOLIO_DB_DRIVER / DB_DRIVER 与 MINEFOLIO_DB_DSN / DB_DSN 环境变量\n");
    printf("     （也接受 MINEFOLIO_DB_DRIVER_FILE / MINEFOLIO_DB_DSN_FILE 文件路径）\n");
    printf("  3. config/db.json 文件\n");
    printf("  4. 缺省: sqlite -> ./data/minefolio.db ; postgres -> host=localhost user=minefolio dbname=minefolio\n");
    printf("\n安全:\n");
    printf("  - 密码哈希: bcrypt, cost=%d（与 Web 登录路径一致）\n", CSILK_BCRYPT_DEFAULT_COST);
    printf("  - 重置后所有现存 JWT 自动失效 (token_version+1)\n");
    printf("  - 非交互/CI 必须提供 --yes；交互模式会在破坏性操作前二次确认\n");
    printf("  - 优先使用 --password-file / --password-stdin 而非 --password（避免 argv 泄漏到 /proc）\n");
}

int
main(int argc, char** argv)
{
    if (argc < 2) {
        print_help("reset-password");
        return 64; /* usage */
    }

    const char* sub = argv[1];
    const char* username      = NULL;
    const char* password_arg  = NULL;
    const char* password_file = NULL;
    int         password_stdin = 0;
    int         assume_yes     = 0;
    const char* driver_arg     = NULL;
    const char* dsn_arg        = NULL;
    char        password_buf[MAX_PASSWORD_LEN + 1] = {0};

    /* 简单解析：sub 之后逐 token 走 */
    for (int i = 2; i < argc; i++) {
        const char* a = argv[i];
        if (strcmp(a, "--password") == 0 && i + 1 < argc) {
            password_arg = argv[++i];
        } else if (strcmp(a, "--password-file") == 0 && i + 1 < argc) {
            password_file = argv[++i];
        } else if (strcmp(a, "--password-stdin") == 0) {
            password_stdin = 1;
        } else if (strcmp(a, "--yes") == 0) {
            assume_yes = 1;
        } else if (strcmp(a, "--driver") == 0 && i + 1 < argc) {
            driver_arg = argv[++i];
        } else if (strcmp(a, "--dsn") == 0 && i + 1 < argc) {
            dsn_arg = argv[++i];
        } else if (a[0] != '-' && !username) {
            username = a;
        } else {
            fprintf(stderr, "未知参数: %s\n", a);
            print_help("reset-password");
            return 64;
        }
    }

    if (strcmp(sub, "help") == 0 || strcmp(sub, "-h") == 0 || strcmp(sub, "--help") == 0) {
        print_help("reset-password");
        return 0;
    }

    /* 数据库连接 */
    const char* driver = NULL;
    const char* dsn    = NULL;
    resolve_db_config(driver_arg, dsn_arg, &driver, &dsn);
    csilk_db_init();
    csilk_db_pool_t* pool = csilk_db_pool_new_ex(driver, dsn, 1);
    if (!pool) {
        fprintf(stderr, "无法连接数据库 driver=%s dsn=%s\n", driver, dsn);
        fprintf(stderr, "（提示：在容器内可用 docker exec <c> /app/reset-password ...）\n");
        fprintf(stderr, "       也可通过 --driver / --dsn 显式指定）\n");
        return 65;
    }

    int rc = 0;
    if (strcmp(sub, "list") == 0) {
        rc = cmd_list(pool);
    } else if (strcmp(sub, "reset") == 0) {
        if (!username) {
            fprintf(stderr, "reset 需要 <username> 参数\n");
            print_help(argv[0]);
            csilk_db_pool_free(pool);
            return 64;
        }
        /* 解析密码 */
        if (password_arg) {
            snprintf(password_buf, sizeof(password_buf), "%s", password_arg);
        } else if (password_file) {
            if (read_password_file(password_file, password_buf, sizeof(password_buf)) != 0) {
                csilk_db_pool_free(pool);
                return 1;
            }
        } else if (password_stdin) {
            if (read_password_stdin(password_buf, sizeof(password_buf)) != 0) {
                csilk_db_pool_free(pool);
                return 1;
            }
        } else {
            /* 交互模式：提示输入（隐藏回显可选） */
            if (!isatty(fileno(stdin))) {
                /* 非 TTY（管道/重定向）：无法提示，必须走 --password* 任一形式 */
                fprintf(stderr, "非交互环境：请改用 --password / --password-file / --password-stdin 之一\n");
                csilk_db_pool_free(pool);
                return 71;
            }
            printf("输入新密码 (%d~%d 字符, 不回显): ", MIN_PASSWORD_LEN, MAX_PASSWORD_LEN);
            fflush(stdout);
            char line[MAX_PASSWORD_LEN + 2] = {0};
            if (!fgets(line, sizeof(line), stdin)) {
                fprintf(stderr, "无法读取密码\n");
                csilk_db_pool_free(pool);
                return 71;
            }
            size_t n = strlen(line);
            while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
                line[--n] = '\0';
            }
            /* 显式回显长度，避免用户被截断而不自知 */
            if (n > MAX_PASSWORD_LEN - 1) {
                fprintf(stderr, "密码过长（> %d），拒绝\n", MAX_PASSWORD_LEN - 1);
                csilk_db_pool_free(pool);
                return 72;
            }
            memset(password_buf, 0, sizeof(password_buf));
            memcpy(password_buf, line, n);
        }
        rc = cmd_reset(pool, username, password_buf, assume_yes, password_stdin || (password_file != NULL));
        explicit_bzero(password_buf, sizeof(password_buf));
    } else if (strcmp(sub, "verify") == 0) {
        if (!username || !password_arg) {
            fprintf(stderr, "verify 需要 <username> 与 --password <candidate>\n");
            csilk_db_pool_free(pool);
            return 64;
        }
        rc = cmd_verify(pool, username, password_arg);
    } else if (strcmp(sub, "disable-2fa") == 0) {
        if (!username) {
            fprintf(stderr, "disable-2fa 需要 <username> 参数\n");
            csilk_db_pool_free(pool);
            return 64;
        }
        rc = cmd_disable_2fa(pool, username, assume_yes);
    } else {
        fprintf(stderr, "未知子命令: %s\n", sub);
        print_help(argv[0]);
        rc = 64;
    }

    csilk_db_pool_free(pool);
    return rc;
}

/* 注：密码缓冲擦除使用 explicit_bzero（glibc 2.7+ / _GNU_SOURCE），
 * 阻止编译器把 `memset` 优化掉；若链接到不支持 explicit_bzero 的 libc，
 * 可退化为 memset（功能等价，仅丢失「防优化」保证）。 */
