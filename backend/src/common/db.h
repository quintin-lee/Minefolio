#pragma once

/**
 * @file db.h
 * @brief 数据库连接池生命周期、迁移执行及结果集字段类型转换工具接口
 *
 * 封装底层 csilk_db 连接池单例的初始化、PostgreSQL/SQLite 驱动适配、
 * 数据库多版本热迁移执行逻辑，以及从 JSON 结果集安全提取数值/布尔类型的辅助工具。
 */

#include "csilk/drivers/db.h"
#include "core/financial/currency.h"
#include "core/financial/decimal.h"
#include "core/financial/money.h"
#include "core/financial/quantity.h"
#include "core/financial/price.h"
#include "core/financial/rate.h"
#include "core/financial/percentage.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>

/**
 * @brief 初始化全局数据库连接池单例
 *
 * 根据环境变量（MINEFOLIO_DB_DRIVER, MINEFOLIO_DB_DSN）或配置文件（config/db.json）
 * 加载数据库驱动（SQLite 或 PostgreSQL），建立连接池并配置基础 PRAGMA 优化参数（如 WAL 模式、忙时等待等）。
 *
 * @param[out] out_pool 接收新创建的数据库连接池实例指针的地址
 *
 * @return int 状态码
 * @retval 0 初始化成功
 * @retval -1 初始化失败（驱动不支持、连接无法建立或配置错误）
 *
 * @note 内存所有权：内部维护静态全局连接池单例，并通过 out_pool 输出，进程退出前应适时析构。
 * @note 线程安全性：应在进程单线程启动阶段调用一次。
 */
int db_init(csilk_db_pool_t** out_pool);

/**
 * @brief 针对当前数据库连接池执行全量与增量 SQL 数据迁移
 *
 * 自动检测当前数据库类型（SQLite 或 PostgreSQL），读取对应迁移脚本（migration.sql / migration_postgres.sql），
 * 幂等执行表结构初始化、字段追加（ALTER TABLE）、外键重构与基础数据回填。
 *
 * @param[in] pool 数据库连接池指针，不可为 NULL
 *
 * @return int 状态码
 * @retval 0 迁移执行成功
 * @retval -1 迁移失败（脚本文件未找到或 SQL 执行出错）
 *
 * @note 线程安全性：应在系统单线程启动阶段执行，避免与业务读写并发冲突。
 */
int db_run_migrations(csilk_db_pool_t* pool);

/**
 * @brief 获取已初始化的全局数据库连接池单例指针
 *
 * @return csilk_db_pool_t* 全局连接池指针；若未初始化则返回 NULL
 *
 * @note 内存所有权：返回内部单例指针，严禁由外部调用者随意释放。
 * @note 线程安全性：只读访问全局指针，初始化完成后线程安全。
 */
csilk_db_pool_t* db_get_pool(void);

/**
 * @brief 查询当前数据库驱动是否为 PostgreSQL
 *
 * @return int 驱动标识
 * @retval 1 当前运行于 PostgreSQL 模式
 * @retval 0 当前运行于 SQLite 模式
 *
 * @note 线程安全性：只读访问，线程安全。
 */
int db_is_postgres(void);

/* =========================================================================
 * 事务作用域与保存点管理 (Transaction Scope & Savepoint Guard)
 * ========================================================================= */

typedef csilk_db_tx_scope_t db_tx_scope_t;

/**
 * @brief 检测当前底层连接是否处于活跃事务块中
 * @param pool 数据库连接池指针
 * @return 1 处于事务中，0 处于 autocommit 模式
 */
int db_in_transaction(csilk_db_pool_t* pool);

/**
 * @brief 开启事务或嵌套保存点作用域
 * 若当前已在事务中，则创建名为 name 的 SAVEPOINT；
 * 若当前不在事务中，则执行 BEGIN TRANSACTION。
 *
 * @param pool 数据库连接池
 * @param name 保存点前缀名称（如 mf_ledger_apply_tx）
 * @param scope 输出作用域控制块
 * @return 0 成功，-1 失败
 */
int db_tx_scope_begin(csilk_db_pool_t* pool, const char* name, db_tx_scope_t* scope);

/**
 * @brief 提交事务或释放保存点
 * 若为保存点，执行 RELEASE SAVEPOINT <name>；
 * 若为顶层事务，执行 COMMIT。
 *
 * @param pool 数据库连接池
 * @param scope 作用域控制块
 * @return 0 成功，-1 失败
 */
int db_tx_scope_commit(csilk_db_pool_t* pool, db_tx_scope_t* scope);

/**
 * @brief 回滚事务或回滚保存点
 * 若为保存点，执行 ROLLBACK TO SAVEPOINT <name> 并释放该保存点；
 * 若为顶层事务，执行 ROLLBACK。
 *
 * @param pool 数据库连接池
 * @param scope 作用域控制块
 * @return 0 成功，-1 失败
 */
int db_tx_scope_rollback(csilk_db_pool_t* pool, db_tx_scope_t* scope);

/**
 * @section csilk_json_db_contract csilk JSON 所有权模型与 DB 驱动层类型契约
 *
 * 【重要约定 - 请在修改任何仓库层/用例层 JSON 处理前通读】
 *
 * 1. 结果集类型因驱动而异（csilk#2 修复后的现状）：
 *    - SQLite 驱动（deps/csilk/src/drivers/db/sqlite.c 的 sqlite_stmt_query_json）
 *      按列的真实类型建节点：SQLITE_INTEGER → int 节点、SQLITE_FLOAT → number 节点、
 *      SQLITE_NULL → null 节点、SQLITE_TEXT/BLOB → string 节点。
 *    - PostgreSQL 驱动（postgres.c 的 pg_result_to_json）仍把每个非 NULL 列
 *      一律建成 string 节点。
 *    - csilk_db_query_json（无参数旧路径）同样全字符串化。
 *    因此**同一份 repository 代码在 SQLite 下可能拿到 int，在 PG 下拿到 string**。
 *
 * 2. 读取任何 DB 字段必须走 db_get_num / db_get_int / db_get_bool / db_get_decimal：
 *    它们对 int / number / string / bool 节点透明解析，是消除上述双引擎差异的唯一正确入口。
 *    严禁直接调用 csilk_json_get_number()（对 string 节点返回 0.0）或
 *    csilk_json_get_int()，也严禁假设某字段"一定是字符串/一定是数字"。
 *
 * 3. DB 中的 JSON 复合列（tags、config_json、metadata 等）：
 *    两条路径下它们都是【未解析的 JSON 字符串标量】——SQLite 驱动把 TEXT/BLOB
    列建成 string 节点，PG 驱动把全部列建成 string 节点。交付前端前必须
    csilk_json_parse() 反序列化后挂载为真正的 array/object。
 *    统一做法：先看 csilk_json_is_array/is_object，命中则深拷贝使用，
 *    否则取字符串再 parse——即 daily_expense_repo_impl.c 里
 *    mf_format_expense_rows_with_tags 的既有双分支写法。
 *    若要驱动层直接产出原生 array/object，可用 csilk_db_query_param_json_with_columns()
    显式声明 JSON 列名，避免每处手写 parse。
 *
 * 4. 容器挂载转移所有权（Ownership Transfer）：
 *    调用 csilk_json_array_append(arr, item) 或 csilk_json_add_object(obj, key, child) 会转移 child 的所有权给父容器。
 *    父容器释放时会递归级联释放所有子节点。严禁在挂载后对子节点单独调用 csilk_json_free()（会导致 Double-Free）。
 *
 * 5. 借用视图与跨文档深拷贝（View vs Deep-Copy）：
 *    csilk_json_get(obj, key) 与 csilk_json_array_get(arr, idx) 返回的是借用视图指针（Borrowed View），
 *    其生命周期与原文档绑定，严禁单独释放。
 *    若需将一个文档中的子节点挂载到另一个独立文档中，必须调用 csilk_json_copy(view) 执行跨文档深拷贝；
 *    严禁跨文档直接复用指针挂载，否则原文档被 free 后会导致悬空引用与 Use-After-Free (UAF)。
 */

/**
 * @brief 从查询结果行的 JSON 对象中安全提取浮点数值
 *
 * 封装底层 csilk_json_as_double，支持透明解析 number、boolean 以及文本型数值。
 *
 * @param[in] obj 查询结果行 JSON 对象指针
 * @param[in] key 列名/字段键名
 *
 * @return double 解析得到的浮点数值；若字段不存在或为空则返回 0.0
 *
 * @note 线程安全性：纯只读解析，线程安全。
 */
static inline double
db_get_num(const csilk_json_t* obj, const char* key)
{
    return csilk_json_as_double(csilk_json_get(obj, key), 0.0);
}

/**
 * @brief 从查询结果行的 JSON 对象中安全提取 64 位整型数值
 *
 * 封装底层 csilk_json_as_int64，支持透明解析 integer、number、boolean 以及文本型数值。
 *
 * @param[in] obj 查询结果行 JSON 对象指针
 * @param[in] key 列名/字段键名
 *
 * @return int64_t 解析得到的整数；若字段不存在或为空则返回 0
 *
 * @note 线程安全性：纯只读解析，线程安全。
 */
static inline int64_t
db_get_int(const csilk_json_t* obj, const char* key)
{
    return csilk_json_as_int64(csilk_json_get(obj, key), 0);
}

/**
 * @brief 从查询结果行的 JSON 对象中安全提取布尔值
 *
 * 封装底层 csilk_json_as_bool，支持识别 JSON 布尔节点、数值非 0、以及文本字符串。
 *
 * @param[in] obj 查询结果行 JSON 对象指针
 * @param[in] key 列名/字段键名
 *
 * @return int 布尔值（1 表示 true，0 表示 false）
 *
 * @note 线程安全性：纯只读解析，线程安全。
 */
static inline int
db_get_bool(const csilk_json_t* obj, const char* key)
{
    return csilk_json_as_bool(csilk_json_get(obj, key), false) ? 1 : 0;
}

/**
 * @brief 从查询结果行的 JSON 对象中安全提取高精度 Decimal 定点数
 *
 * 优先读取原始字符串（保持完全精度），若为数值节点则转换为 Decimal。
 *
 * @param[in] obj 查询结果行 JSON 对象指针
 * @param[in] key 字段键名
 * @return decimal_t 解析得到的 Decimal 定点数；若不存在则返回 zero
 */
static inline decimal_t
db_get_decimal(const csilk_json_t* obj, const char* key)
{
    const csilk_json_t* v = csilk_json_get(obj, key);
    if (!v) {
        return decimal_zero();
    }
    if (csilk_json_is_string(v)) {
        const char* s = csilk_json_string_value(v);
        if (!s) {
            return decimal_zero();
        }
        decimal_t d;
        if (decimal_from_string(s, &d) == DECIMAL_OK) {
            return d;
        }
        return decimal_zero();
    }
    if (csilk_json_is_number(v)) {
        double    num = csilk_json_number_value(v);
        decimal_t d;
        decimal_from_double(num, 4, &d);
        return d;
    }
    return decimal_zero();
}

/**
 * @brief 从查询结果行的 JSON 对象中安全提取指定币种的 money_t 货币金额
 */
static inline money_t
db_get_money(const csilk_json_t* obj, const char* key, currency_t cur)
{
    decimal_t d = db_get_decimal(obj, key);
    return money_from_decimal(d, cur);
}

/**
 * @brief 从查询结果行的 JSON 对象中安全提取 quantity_t 标的份额数量
 */
static inline quantity_t
db_get_quantity(const csilk_json_t* obj, const char* key)
{
    decimal_t d = db_get_decimal(obj, key);
    return quantity_from_decimal(d);
}

/**
 * @brief 从查询结果行的 JSON 对象中安全提取 price_t 单价
 */
static inline price_t
db_get_price(const csilk_json_t* obj, const char* key, currency_t cur)
{
    decimal_t d = db_get_decimal(obj, key);
    return price_from_decimal(d, cur);
}

/**
 * @brief 从查询结果行的 JSON 对象中安全提取 rate_t 汇率
 */
static inline rate_t
db_get_rate(const csilk_json_t* obj, const char* key, currency_t from_cur, currency_t to_cur)
{
    decimal_t d = db_get_decimal(obj, key);
    return rate_from_decimal(d, from_cur, to_cur);
}

/**
 * @brief 从查询结果行的 JSON 对象中安全提取 percentage_t 百分比
 */
static inline percentage_t
db_get_percentage(const csilk_json_t* obj, const char* key)
{
    decimal_t d = db_get_decimal(obj, key);
    return percentage_from_decimal(d);
}

/**
 * @brief 对浮点数执行确定性精度四舍五入，消除 IEEE-754 尾部毛刺
 */
static inline double
mf_round_to_decimals(double val, int decimals)
{
    double factor = 1.0;
    for (int i = 0; i < decimals; i++) {
        factor *= 10.0;
    }
    return round(val * factor) / factor;
}
