/**
 * @file transaction_repo.c
 * @brief 核心交易流水与手续费级联管理数据访问层实现
 *
 * 实现了交易流水的复杂多条件分页筛选（含三表 LEFT JOIN 联查）、
 * 月度出入金流动性统计、旧持仓参数快照检索、以及 parent_tx_id 手续费子行的独立查询与级联删除。
 */

#include "repositories/transaction_repo.h"
#include "common/db.h"
#include <stdio.h>
#include <string.h>

/**
 * @brief 多条件动态拼接分页查询交易流水
 *
 * 执行流程：
 * 1. 动态拼接 WHERE 子句（asset_id, category_id, transaction_type, source_type, start_date, end_date）。
 * 2. 统计总匹配数并填充 `*total`。
 * 3. 联查主资产名称 (a.name)、关联资金账户名称 (la.name) 及分类名称 (c.name)。
 * 4. 按交易发生日期降序 (`ORDER BY t.transaction_date DESC`) 分页返回。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param page 页码
 * @param page_size 每页数量
 * @param asset_id 资产 ID 筛选
 * @param category_id 分类 ID 筛选
 * @param type 交易类型筛选
 * @param source_type 来源类型筛选
 * @param start_date 起始日期
 * @param end_date 截止日期
 * @param[out] total 输出参数，符合条件的总记录数
 * @return csilk_json_t* 交易记录 JSON 数组
 */
csilk_json_t*
tx_list(csilk_db_pool_t* pool,
        int64_t          user_id,
        int64_t          page,
        int64_t          page_size,
        const char*      asset_id,
        const char*      category_id,
        const char*      type,
        const char*      source_type,
        const char*      start_date,
        const char*      end_date,
        int64_t*         total)
{
    char uid[32], limit[32], offset[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(limit, sizeof(limit), "%lld", (long long)page_size);
    snprintf(offset, sizeof(offset), "%lld", (long long)((page - 1) * page_size));
    char        sql[1024], count_sql[1024];
    const char *params[16], *cnt_params[16];
    int         pidx = 0, cnt_pidx = 0;
    params[pidx++] = uid;
    cnt_params[cnt_pidx++] = uid;
    snprintf(sql,
             sizeof(sql),
             "SELECT "
             "t.id,t.asset_id,t.linked_asset_id,t.category_id,t.transaction_type,t.source_type,t."
             "direction,t.linked_direction,t.amount,t.price_per_unit,t.quantity,t.currency,t."
             "transaction_date,t.note,a.name as asset_name,la.name as linked_asset_name,c.name as "
             "category_name FROM transactions t LEFT JOIN assets a ON t.asset_id=a.id LEFT JOIN "
             "assets la ON t.linked_asset_id=la.id LEFT JOIN categories c ON t.category_id=c.id "
             "WHERE t.user_id=?");
    snprintf(count_sql,
             sizeof(count_sql),
             "SELECT COUNT(*) AS cnt FROM transactions t WHERE t.user_id=?");
#define AF(x)                                                                                      \
    do {                                                                                           \
        strncat(sql, " AND t." x "=?", sizeof(sql) - strlen(sql) - 1);                             \
        strncat(count_sql, " AND t." x "=?", sizeof(count_sql) - strlen(count_sql) - 1);           \
        params[pidx++] = x;                                                                        \
        cnt_params[cnt_pidx++] = x;                                                                \
    } while (0)
    if (asset_id) {
        AF("asset_id");
    }
    if (category_id) {
        AF("category_id");
    }
    if (type) {
        AF("transaction_type");
    }
    if (source_type) {
        AF("source_type");
    }
    if (start_date) {
        strncat(sql, " AND t.transaction_date >= ?", sizeof(sql) - strlen(sql) - 1);
        strncat(
            count_sql, " AND t.transaction_date >= ?", sizeof(count_sql) - strlen(count_sql) - 1);
        params[pidx++] = start_date;
        cnt_params[cnt_pidx++] = start_date;
    }
    if (end_date) {
        strncat(sql, " AND t.transaction_date <= ?", sizeof(sql) - strlen(sql) - 1);
        strncat(
            count_sql, " AND t.transaction_date <= ?", sizeof(count_sql) - strlen(count_sql) - 1);
        params[pidx++] = end_date;
        cnt_params[cnt_pidx++] = end_date;
    }
#undef AF
    strncat(
        sql, " ORDER BY t.transaction_date DESC LIMIT ? OFFSET ?", sizeof(sql) - strlen(sql) - 1);
    params[pidx++] = limit;
    params[pidx++] = offset;
    params[pidx] = NULL;
    cnt_params[cnt_pidx] = NULL;

    /* 1. 统计符合条件的总记录数 */
    csilk_json_t* cnt_res = csilk_db_query_param_json(pool, count_sql, cnt_params);
    *total = 0;
    if (cnt_res && csilk_json_array_size(cnt_res) > 0) {
        *total = db_get_int(csilk_json_array_get(cnt_res, 0), "cnt");
    }
    if (cnt_res) {
        csilk_json_free(cnt_res);
    }

    /* 2. 执行分页查询 */
    return csilk_db_query_param_json(pool, sql, params);
}

/**
 * @brief 按月份统计总流水、资金流入流出与总笔数
 *
 * 执行 SQL：
 * `SELECT COALESCE(SUM(amount),0) AS total_volume, COALESCE(SUM(CASE WHEN direction='in' THEN amount ELSE 0 END),0) AS inflows, COALESCE(SUM(CASE WHEN direction='out' THEN amount ELSE 0 END),0) AS outflows, COUNT(*) AS count FROM transactions WHERE user_id=? AND transaction_date LIKE ?`
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param pattern 日期通配符 (如 "2026-09%")
 * @return csilk_json_t* 汇总指标 JSON 数组
 */
csilk_json_t*
tx_monthly(csilk_db_pool_t* pool, int64_t user_id, const char* pattern)
{
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    return csilk_db_query_param_json(
        pool,
        "SELECT COALESCE(SUM(amount),0) AS total_volume,COALESCE(SUM(CASE WHEN direction='in' THEN "
        "amount ELSE 0 END),0) AS inflows,COALESCE(SUM(CASE WHEN direction='out' THEN amount ELSE "
        "0 END),0) AS outflows,COUNT(*) AS count FROM transactions WHERE user_id=? AND "
        "transaction_date LIKE ?",
        (const char*[]){uid, pattern, NULL});
}

/**
 * @brief 插入新的交易记录
 *
 * 执行 SQL：
 * `INSERT INTO transactions (user_id,asset_id,linked_asset_id,category_id,source_type,transaction_type,direction,linked_direction,amount,price_per_unit,quantity,fee,currency,transaction_date,note) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) RETURNING id`
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param asset_id 主资产 ID
 * @param linked_asset_id 关联资金资产 ID (0 则存为 NULL)
 * @param category_id 分类 ID
 * @param source_type 来源类型
 * @param transaction_type 交易类型
 * @param direction 主资产资金方向
 * @param linked_direction 关联账户资金方向
 * @param amount 金额
 * @param price_per_unit 单价
 * @param quantity 份额
 * @param fee 手续费
 * @param currency 货币
 * @param date 日期
 * @param note 备注
 * @return int64_t 成功生成的主键 ID，失败返回 0
 */
int64_t
tx_insert(csilk_db_pool_t* pool,
          int64_t          user_id,
          int64_t          asset_id,
          int64_t          linked_asset_id,
          int64_t          category_id,
          const char*      source_type,
          const char*      transaction_type,
          const char*      direction,
          const char*      linked_direction,
          double           amount,
          double           price_per_unit,
          double           quantity,
          double           fee,
          const char*      currency,
          const char*      date,
          const char*      note)
{
    char uid[32], ast[32], last[32], cat[32], amt[64], pp[64], qty[64], fee_s[64];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(ast, sizeof(ast), "%lld", (long long)asset_id);
    snprintf(last, sizeof(last), "%lld", (long long)linked_asset_id);
    snprintf(cat, sizeof(cat), "%lld", (long long)category_id);
    snprintf(amt, sizeof(amt), "%.6f", amount);
    snprintf(pp, sizeof(pp), "%.4f", price_per_unit);
    snprintf(qty, sizeof(qty), "%.4f", quantity);
    snprintf(fee_s, sizeof(fee_s), "%.6f", fee);
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "INSERT INTO transactions "
        "(user_id,asset_id,linked_asset_id,category_id,source_type,transaction_type,direction,"
        "linked_direction,amount,price_per_unit,quantity,fee,currency,transaction_date,note) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) RETURNING id",
        (const char*[]){uid,
                        ast,
                        linked_asset_id > 0 ? last : "NULL",
                        cat,
                        source_type,
                        transaction_type,
                        direction ? "in" : "out",
                        linked_direction ? "out" : NULL,
                        amt,
                        pp,
                        qty,
                        fee_s,
                        currency ? currency : "CNY",
                        date,
                        note ? note : "",
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
 * @brief 更新交易流水记录
 *
 * 执行 SQL：
 * `UPDATE transactions SET transaction_type=?,direction=?,linked_direction=?,amount=?,price_per_unit=?,quantity=?,currency=?,transaction_date=?,note=?,category_id=?,source_type=?,linked_asset_id=NULLIF(?, '0') WHERE id=? AND user_id=? RETURNING id`
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 交易 ID
 * @param transaction_type 交易类型
 * @param direction 主方向
 * @param linked_direction 关联方向
 * @param amount 金额
 * @param price_per_unit 单价
 * @param quantity 份额
 * @param currency 货币
 * @param date 日期
 * @param note 备注
 * @param category_id 分类 ID
 * @param source_type 来源类型
 * @param linked_asset_id 关联账户 ID
 * @return int 成功返回 1，失败返回 0
 */
int
tx_update(csilk_db_pool_t* pool,
          int64_t          user_id,
          int64_t          id,
          const char*      transaction_type,
          const char*      direction,
          const char*      linked_direction,
          double           amount,
          double           price_per_unit,
          double           quantity,
          const char*      currency,
          const char*      date,
          const char*      note,
          int64_t          category_id,
          const char*      source_type,
          int64_t          linked_asset_id)
{
    char uid[32], idstr[32], amt[64], pp[64], qty[64], cat[32], last[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    snprintf(amt, sizeof(amt), "%.6f", amount);
    snprintf(pp, sizeof(pp), "%.4f", price_per_unit);
    snprintf(qty, sizeof(qty), "%.4f", quantity);
    snprintf(cat, sizeof(cat), "%lld", (long long)category_id);
    snprintf(last, sizeof(last), "%lld", (long long)linked_asset_id);
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "UPDATE transactions SET "
        "transaction_type=?,direction=?,linked_direction=?,amount=?,price_per_unit=?,quantity=?,"
        "currency=?,transaction_date=?,note=?,category_id=?,source_type=?,linked_asset_id=NULLIF(?,"
        " '0') WHERE id=? AND user_id=? RETURNING id",
        (const char*[]){transaction_type,
                        direction ? "in" : "out",
                        linked_direction ? "out" : NULL,
                        amt,
                        pp,
                        qty,
                        currency ? currency : "CNY",
                        date ? date : "",
                        note ? note : "",
                        cat,
                        source_type ? source_type : "expense",
                        last,
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
 * @brief 查询交易变更前的原始关键字段快照
 *
 * 执行 SQL：`SELECT asset_id,linked_asset_id,amount,transaction_type,quantity,price_per_unit,fee FROM transactions WHERE id=? AND user_id=?`
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 交易 ID
 * @return csilk_json_t* 快照字段 JSON 数组
 */
csilk_json_t*
tx_get_old(csilk_db_pool_t* pool, int64_t user_id, int64_t id)
{
    char uid[32], idstr[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    return csilk_db_query_param_json(
        pool,
        "SELECT asset_id,linked_asset_id,amount,transaction_type,quantity,price_per_unit,fee FROM "
        "transactions WHERE id=? AND user_id=?",
        (const char*[]){idstr, uid, NULL});
}

/**
 * @brief 查询指定父交易关联的所有手续费子行 (Fee Child Rows)
 *
 * 执行 SQL：`SELECT id, linked_asset_id, amount, note FROM transactions WHERE parent_tx_id=? AND user_id=? AND transaction_type='fee'`
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param parent_tx_id 父交易 ID
 * @return csilk_json_t* 手续费子记录 JSON 数组
 */
csilk_json_t*
tx_child_fee_rows(csilk_db_pool_t* pool, int64_t user_id, int64_t parent_tx_id)
{
    char uid[32], pid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(pid, sizeof(pid), "%lld", (long long)parent_tx_id);
    return csilk_db_query_param_json(
        pool,
        "SELECT id, linked_asset_id, amount, note "
        "FROM transactions WHERE parent_tx_id=? AND user_id=? AND transaction_type='fee'",
        (const char*[]){pid, uid, NULL});
}

/**
 * @brief 级联删除指定父交易下的所有手续费子行
 *
 * 执行 SQL：`DELETE FROM transactions WHERE parent_tx_id=? AND user_id=? AND transaction_type='fee' RETURNING id`
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param parent_tx_id 父交易 ID
 * @return int 成功删除返回 1，否则返回 0
 */
int
tx_delete_fee_children(csilk_db_pool_t* pool, int64_t user_id, int64_t parent_tx_id)
{
    char uid[32], pid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(pid, sizeof(pid), "%lld", (long long)parent_tx_id);
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "DELETE FROM transactions WHERE parent_tx_id=? AND user_id=? AND transaction_type='fee' "
        "RETURNING id",
        (const char*[]){pid, uid, NULL});
    int ok = res ? csilk_json_array_size(res) > 0 : 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}

/**
 * @brief 删除主交易记录
 *
 * 执行 SQL：`DELETE FROM transactions WHERE id=? AND user_id=? RETURNING id`
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 交易 ID
 * @return int 成功删除返回 1，否则返回 0
 */
int
tx_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id)
{
    char uid[32], idstr[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    csilk_json_t* res =
        csilk_db_query_param_json(pool,
                                  "DELETE FROM transactions WHERE id=? AND user_id=? RETURNING id",
                                  (const char*[]){idstr, uid, NULL});
    int ok = res ? csilk_json_array_size(res) > 0 : 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}

/**
 * @brief 校验资产是否存在且属于该用户
 *
 * 执行 SQL：`SELECT id FROM assets WHERE id=? AND user_id=?`
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param asset_id 资产 ID
 * @return int 存在返回 1，不存在返回 0
 */
int
tx_asset_exists(csilk_db_pool_t* pool, int64_t user_id, int64_t asset_id)
{
    char uid[32], ast[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(ast, sizeof(ast), "%lld", (long long)asset_id);
    csilk_json_t* res = csilk_db_query_param_json(
        pool, "SELECT id FROM assets WHERE id=? AND user_id=?", (const char*[]){ast, uid, NULL});
    int ok = res && csilk_json_array_size(res) > 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}
