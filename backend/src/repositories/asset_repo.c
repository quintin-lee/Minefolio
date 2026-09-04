/**
 * @file asset_repo.c
 * @brief 资产数据访问层具体实现
 *
 * 实现了资产表 (assets) 与分类表 (categories) 的联查分页、资产增删改查、
 * 投资持仓参数维护、行情价格回填与自动市值重算等 SQL 执行逻辑。
 */

#include "repositories/asset_repo.h"
#include "common/db.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/**
 * @brief 分页查询用户的资产列表，支持按分类筛选
 *
 * 执行逻辑：
 * 1. 格式化分页参数 (limit, offset) 及 user_id。
 * 2. 判断是否存在 `category_id`，构造对应的 COUNT 查询和数据查询 SQL。
 * 3. 左连接 categories 表读取分类名称 (category_name) 与资产类型 (asset_type)。
 * 4. 执行总数查询填充 `*total`，再执行分页数据查询返回结果。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param page 页码
 * @param page_size 每页条数
 * @param category_id 可选分类 ID
 * @param[out] total 记录总数指针
 * @return csilk_json_t* 资产列表 JSON 数组
 */
csilk_json_t*
asset_list(csilk_db_pool_t* pool,
           int64_t          user_id,
           int64_t          page,
           int64_t          page_size,
           const char*      category_id,
           int64_t*         total)
{
    char uid[32], limit[32], offset[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(limit, sizeof(limit), "%lld", (long long)page_size);
    snprintf(offset, sizeof(offset), "%lld", (long long)((page - 1) * page_size));
    char        sql[1024], count_sql[512];
    const char *params[8], *cnt_params[4];
    int         pidx = 0, cnt_pidx = 0;
    params[pidx++] = uid;
    cnt_params[cnt_pidx++] = uid;
    if (category_id && category_id[0]) {
        snprintf(
            sql,
            sizeof(sql),
            "SELECT "
            "a.id,a.category_id,a.name,a.account_no,a.symbol,a.quote_source,CAST(a.last_sync_at AS "
            "TEXT) as last_sync_at,"
            "a.current_value,a.currency,a.note,a.created_at,"
            "a.updated_at,c.name as category_name,c.asset_type,a.quantity,a.cost_basis,a.net_value "
            "FROM assets a LEFT JOIN categories c ON a.category_id=c.id WHERE a.user_id=? AND "
            "a.category_id=? ORDER BY c.name,a.name LIMIT ? OFFSET ?");
        snprintf(count_sql,
                 sizeof(count_sql),
                 "SELECT COUNT(*) AS cnt FROM assets a WHERE a.user_id=? AND a.category_id=?");
        params[pidx++] = category_id;
        cnt_params[cnt_pidx++] = category_id;
    } else {
        snprintf(
            sql,
            sizeof(sql),
            "SELECT "
            "a.id,a.category_id,a.name,a.account_no,a.symbol,a.quote_source,CAST(a.last_sync_at AS "
            "TEXT) as last_sync_at,"
            "a.current_value,a.currency,a.note,a.created_at,"
            "a.updated_at,c.name as category_name,c.asset_type,a.quantity,a.cost_basis,a.net_value "
            "FROM assets a LEFT JOIN categories c ON a.category_id=c.id WHERE a.user_id=? ORDER BY "
            "c.name,a.name LIMIT ? OFFSET ?");
        snprintf(
            count_sql, sizeof(count_sql), "SELECT COUNT(*) AS cnt FROM assets a WHERE a.user_id=?");
    }
    params[pidx++] = limit;
    params[pidx++] = offset;
    params[pidx] = NULL;
    cnt_params[cnt_pidx] = NULL;

    /* 1. 查询总数 */
    csilk_json_t* cnt_res = csilk_db_query_param_json(pool, count_sql, cnt_params);
    *total = 0;
    if (cnt_res && csilk_json_array_size(cnt_res) > 0) {
        *total = db_get_int(csilk_json_array_get(cnt_res, 0), "cnt");
    }
    if (cnt_res) {
        csilk_json_free(cnt_res);
    }

    /* 2. 查询分页数据 */
    return csilk_db_query_param_json(pool, sql, params);
}

/**
 * @brief 获取单个资产详情
 *
 * 执行参数化 SQL：
 * `SELECT a.*, c.name as category_name, c.asset_type FROM assets a LEFT JOIN categories c ON a.category_id=c.id WHERE a.id=? AND a.user_id=?`
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 资产 ID
 * @return csilk_json_t* 资产详情 JSON 数组（长度为 1）
 */
csilk_json_t*
asset_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id)
{
    char uid[32], idstr[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    return csilk_db_query_param_json(
        pool,
        "SELECT "
        "a.id,a.category_id,a.name,a.account_no,a.symbol,a.quote_source,CAST(a.last_sync_at AS "
        "TEXT) as last_sync_at,"
        "a.current_value,a.currency,a.note,a.created_at,a."
        "updated_at,c.name as category_name,c.asset_type,a.quantity,a.cost_basis,a.net_value FROM "
        "assets a LEFT JOIN categories c ON a.category_id=c.id WHERE a.id=? AND a.user_id=?",
        (const char*[]){idstr, uid, NULL});
}

/**
 * @brief 插入新资产账户
 *
 * 格式化数值字段并执行带 `RETURNING id` 的参数化插入。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param category_id 分类 ID
 * @param name 资产名称
 * @param account_no 银行卡号/账号
 * @param current_value 当前估值/余额
 * @param currency 货币代码（默认为 CNY）
 * @param note 备注
 * @param quantity 持仓数量
 * @param cost_basis 持仓总成本
 * @param net_value 单位净值
 * @param symbol 标的代码
 * @param quote_source 行情源标识
 * @return int64_t 成功返回新资产 ID，失败返回 0
 */
int64_t
asset_insert(csilk_db_pool_t* pool,
             int64_t          user_id,
             int64_t          category_id,
             const char*      name,
             const char*      account_no,
             double           current_value,
             const char*      currency,
             const char*      note,
             double           quantity,
             double           cost_basis,
             double           net_value,
             const char*      symbol,
             const char*      quote_source)
{
    char uid[32], cat[32], val[64], qty[64], cb[64], nv[64];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(cat, sizeof(cat), "%lld", (long long)category_id);
    snprintf(val, sizeof(val), "%.6f", current_value);
    snprintf(qty, sizeof(qty), "%.4f", quantity);
    snprintf(cb, sizeof(cb), "%.4f", cost_basis);
    snprintf(nv, sizeof(nv), "%.4f", net_value);
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "INSERT INTO assets "
        "(user_id,category_id,name,account_no,current_value,currency,note,quantity,cost_basis,net_"
        "value,symbol,quote_source) VALUES (?,?,?, ?,?, ?,?,?,?,?, ?,?) RETURNING id",
        (const char*[]){uid,
                        cat,
                        name,
                        account_no ? account_no : "",
                        val,
                        currency ? currency : "CNY",
                        note ? note : "",
                        qty,
                        cb,
                        nv,
                        symbol ? symbol : "",
                        quote_source ? quote_source : "",
                        NULL});
    int64_t id = 0;
    if (res && csilk_json_array_size(res) > 0) {
        id = db_get_int(csilk_json_array_get(res, 0), "id");
    }
    if (res) {
        csilk_json_free(res);
    }
    return id;
}

/**
 * @brief 更新资产基础属性
 *
 * 执行 SQL：`UPDATE assets SET name=?,account_no=?,current_value=?,currency=?,note=?,symbol=?,quote_source=?,updated_at=CURRENT_TIMESTAMP WHERE id=? AND user_id=?`
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 资产 ID
 * @param name 资产名称
 * @param account_no 卡号/账号
 * @param current_value 当前价值
 * @param currency 币种
 * @param note 备注
 * @param symbol 代码
 * @param quote_source 数据源
 * @return int 成功更新返回 1，否则返回 0
 */
int
asset_update_basic(csilk_db_pool_t* pool,
                   int64_t          user_id,
                   int64_t          id,
                   const char*      name,
                   const char*      account_no,
                   double           current_value,
                   const char*      currency,
                   const char*      note,
                   const char*      symbol,
                   const char*      quote_source)
{
    char uid[32], idstr[32], val[64];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    snprintf(val, sizeof(val), "%.6f", current_value);
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "UPDATE assets SET "
        "name=?,account_no=?,current_value=?,currency=?,note=?,symbol=?,"
        "quote_source=?,updated_at=CURRENT_TIMESTAMP WHERE id=? AND user_id=? RETURNING id",
        (const char*[]){name ? name : "",
                        account_no ? account_no : "",
                        val,
                        currency ? currency : "CNY",
                        note ? note : "",
                        symbol ? symbol : "",
                        quote_source ? quote_source : "",
                        idstr,
                        uid,
                        NULL});
    int ok = res ? csilk_json_array_size(res) > 0 : 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}

/**
 * @brief 行情价格更新与自动市值重算
 *
 * 执行带 CASE WHEN 的原子更新语句：
 * 当 `quantity > 0` 时重算 `current_value = ROUND(quantity * new_net_value, 2)`，
 * 否则保留原有的 `current_value`。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID（为 0 时不限定用户）
 * @param asset_id 目标资产 ID
 * @param new_net_value 最新单位净值
 * @return int 数据库受影响行数
 */
int
asset_update_market_quote(csilk_db_pool_t* pool,
                          int64_t          user_id,
                          int64_t          asset_id,
                          double           new_net_value)
{
    char val[64], aid[32], uid[32];
    snprintf(val, sizeof(val), "%.4f", new_net_value);
    snprintf(aid, sizeof(aid), "%lld", (long long)asset_id);
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);

    csilk_json_t* res = NULL;
    if (user_id > 0) {
        res = csilk_db_query_param_json(
            pool,
            "UPDATE assets SET "
            "net_value = ?, "
            "current_value = CASE WHEN quantity > 0 THEN ROUND(quantity * ?, 2) ELSE current_value "
            "END, "
            "last_sync_at = CURRENT_TIMESTAMP, "
            "updated_at = CURRENT_TIMESTAMP "
            "WHERE id = ? AND user_id = ? RETURNING id",
            (const char*[]){val, val, aid, uid, NULL});
    } else {
        res = csilk_db_query_param_json(
            pool,
            "UPDATE assets SET "
            "net_value = ?, "
            "current_value = CASE WHEN quantity > 0 THEN ROUND(quantity * ?, 2) ELSE current_value "
            "END, "
            "last_sync_at = CURRENT_TIMESTAMP, "
            "updated_at = CURRENT_TIMESTAMP "
            "WHERE id = ? RETURNING id",
            (const char*[]){val, val, aid, NULL});
    }
    int updated = res ? (int)csilk_json_array_size(res) : 0;
    if (res) {
        csilk_json_free(res);
    }
    return updated;
}

/**
 * @brief 查询所有可供行情同步的有代码资产
 *
 * 筛选 `symbol IS NOT NULL AND symbol != ''` 的所有投资标的。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID（大于 0 则按用户过滤，否则查询系统所有资产）
 * @return csilk_json_t* 资产列表 JSON 数组
 */
csilk_json_t*
asset_list_for_sync(csilk_db_pool_t* pool, int64_t user_id)
{
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    if (user_id > 0) {
        return csilk_db_query_param_json(
            pool,
            "SELECT id, user_id, category_id, name, symbol, quote_source, "
            "COALESCE(CAST(net_value AS REAL), 0.0) as net_value, "
            "COALESCE(CAST(quantity AS REAL), 0.0) as quantity, "
            "currency "
            "FROM assets WHERE user_id=? AND symbol IS NOT NULL AND symbol != ''",
            (const char*[]){uid, NULL});
    } else {
        return csilk_db_query_json(pool,
                                   "SELECT id, user_id, category_id, name, symbol, quote_source, "
                                   "COALESCE(CAST(net_value AS REAL), 0.0) as net_value, "
                                   "COALESCE(CAST(quantity AS REAL), 0.0) as quantity, "
                                   "currency "
                                   "FROM assets WHERE symbol IS NOT NULL AND symbol != ''");
    }
}

/**
 * @brief 更新投资资产的持仓信息（净值、持仓数量、成本）
 *
 * 执行 SQL：`UPDATE assets SET net_value=?,quantity=?,cost_basis=?,updated_at=CURRENT_TIMESTAMP WHERE id=? AND user_id=?`
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 资产 ID
 * @param net_value 最新单位净值
 * @param quantity 最新持仓份额
 * @param cost_basis 最新总持仓成本
 * @return int 成功返回 1，失败返回 0
 */
int
asset_update_position(csilk_db_pool_t* pool,
                      int64_t          user_id,
                      int64_t          id,
                      double           net_value,
                      double           quantity,
                      double           cost_basis)
{
    char uid[32], idstr[32], nv[64], qty[64], cb[64];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    snprintf(nv, sizeof(nv), "%.4f", net_value);
    snprintf(qty, sizeof(qty), "%.4f", quantity);
    snprintf(cb, sizeof(cb), "%.4f", cost_basis);
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "UPDATE assets SET net_value=?,quantity=?,cost_basis=?,updated_at=CURRENT_TIMESTAMP WHERE "
        "id=? AND user_id=? RETURNING id",
        (const char*[]){nv, qty, cb, idstr, uid, NULL});
    int ok = res ? csilk_json_array_size(res) > 0 : 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}

/**
 * @brief 删除资产账户
 *
 * 执行 SQL：`DELETE FROM assets WHERE id=? AND user_id=?`
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 资产 ID
 * @return int 成功删除返回 1，否则返回 0
 */
int
asset_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id)
{
    char uid[32], idstr[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    csilk_json_t* res =
        csilk_db_query_param_json(pool,
                                  "DELETE FROM assets WHERE id=? AND user_id=? RETURNING id",
                                  (const char*[]){idstr, uid, NULL});
    int ok = res ? csilk_json_array_size(res) > 0 : 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}

/**
 * @brief 校验资产是否存在且归属于该用户
 *
 * 执行 SQL：`SELECT id FROM assets WHERE id=? AND user_id=?`
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 资产 ID
 * @return int 存在返回 1，不存在返回 0
 */
int
asset_exists(csilk_db_pool_t* pool, int64_t user_id, int64_t id)
{
    char uid[32], idstr[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    csilk_json_t* res = csilk_db_query_param_json(
        pool, "SELECT id FROM assets WHERE id=? AND user_id=?", (const char*[]){idstr, uid, NULL});
    int ok = res && csilk_json_array_size(res) > 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}

/**
 * @brief 查询指定分类对应的资产类型 (asset_type)
 *
 * 执行 SQL：`SELECT asset_type FROM categories WHERE id=? AND user_id=?`
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param category_id 分类 ID
 * @return char* 动态分配的 asset_type 字符串副本，未找到返回 NULL
 */
char*
asset_get_category_type(csilk_db_pool_t* pool, int64_t user_id, int64_t category_id)
{
    char uid[32], cat[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(cat, sizeof(cat), "%lld", (long long)category_id);
    csilk_json_t* res =
        csilk_db_query_param_json(pool,
                                  "SELECT asset_type FROM categories WHERE id=? AND user_id=?",
                                  (const char*[]){cat, uid, NULL});
    char* result = NULL;
    if (res && csilk_json_array_size(res) > 0) {
        const char* atype = csilk_json_get_string(csilk_json_array_get(res, 0), "asset_type");
        if (atype) {
            result = strdup(atype);
        }
    }
    if (res) {
        csilk_json_free(res);
    }
    return result;
}

/**
 * @brief 查询特定资产的历史交易记录明细
 *
 * 执行 SQL：`SELECT id,asset_id,transaction_type,amount,quantity,price_per_unit,currency,transaction_date,note,created_at FROM transactions WHERE asset_id=? AND user_id=? ORDER BY transaction_date DESC`
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param asset_id 资产 ID
 * @return csilk_json_t* 交易记录 JSON 数组
 */
csilk_json_t*
asset_transactions(csilk_db_pool_t* pool, int64_t user_id, int64_t asset_id)
{
    char uid[32], aid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(aid, sizeof(aid), "%lld", (long long)asset_id);
    return csilk_db_query_param_json(
        pool,
        "SELECT "
        "id,asset_id,transaction_type,amount,quantity,price_per_unit,currency,transaction_date,"
        "note,created_at FROM transactions WHERE asset_id=? AND user_id=? ORDER BY "
        "transaction_date DESC",
        (const char*[]){aid, uid, NULL});
}

csilk_json_t*
asset_balance_logs_list(csilk_db_pool_t* pool,
                        int64_t          user_id,
                        int64_t          page,
                        int64_t          page_size,
                        const char*      asset_id_str,
                        int64_t*         total)
{
    char uid_str[32], limit_buf[32], offset_buf[32], aid_buf[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(limit_buf, sizeof(limit_buf), "%lld", (long long)page_size);
    snprintf(offset_buf, sizeof(offset_buf), "%lld", (long long)((page - 1) * page_size));

    char        count_sql[256];
    const char* cnt_params[4];
    snprintf(count_sql,
             sizeof(count_sql),
             "SELECT COUNT(*) AS cnt FROM asset_balance_logs abl WHERE abl.user_id=?");
    cnt_params[0] = uid_str;
    int cnt_pidx = 1;

    csilk_json_t* result = NULL;
    if (asset_id_str && strlen(asset_id_str) > 0) {
        snprintf(aid_buf, sizeof(aid_buf), "%lld", atoll(asset_id_str));
        const char* params[] = {uid_str, aid_buf, limit_buf, offset_buf, NULL};
        result = csilk_db_query_param_json(
            pool,
            "SELECT abl.id, abl.asset_id, a.name AS asset_name, abl.user_id, "
            "abl.delta, abl.balance_after, abl.source_type, abl.source_id, "
            "abl.note, abl.created_at "
            "FROM asset_balance_logs abl "
            "LEFT JOIN assets a ON abl.asset_id = a.id "
            "WHERE abl.user_id=? AND abl.asset_id=? "
            "ORDER BY abl.created_at DESC LIMIT ? OFFSET ?",
            params);
        snprintf(count_sql + strlen(count_sql),
                 sizeof(count_sql) - strlen(count_sql),
                 " AND abl.asset_id=?");
        cnt_params[cnt_pidx++] = aid_buf;
    } else {
        const char* params[] = {uid_str, limit_buf, offset_buf, NULL};
        result = csilk_db_query_param_json(
            pool,
            "SELECT abl.id, abl.asset_id, a.name AS asset_name, abl.user_id, "
            "abl.delta, abl.balance_after, abl.source_type, abl.source_id, "
            "abl.note, abl.created_at "
            "FROM asset_balance_logs abl "
            "LEFT JOIN assets a ON abl.asset_id = a.id "
            "WHERE abl.user_id=? "
            "ORDER BY abl.created_at DESC LIMIT ? OFFSET ?",
            params);
    }
    cnt_params[cnt_pidx] = NULL;

    *total = 0;
    if (!result) {
        return NULL;
    }

    csilk_json_t* cnt_res = csilk_db_query_param_json(pool, count_sql, cnt_params);
    if (cnt_res && csilk_json_array_size(cnt_res) > 0) {
        *total = db_get_int(csilk_json_array_get(cnt_res, 0), "cnt");
    }
    if (cnt_res) {
        csilk_json_free(cnt_res);
    }

    return result;
}
