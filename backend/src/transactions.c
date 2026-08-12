#include "common/response.h"
#include "common/db.h"
#include "common/jwt.h"
#include "common/balance.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief 计算交易对资产余额的业务方向 delta（transfer_* 返回 0）。 */
static double tx_delta(const char* type, double amount, double price, double qty) {
    if (!type) return 0;
    if (strcmp(type, "transfer_in") == 0 || strcmp(type, "transfer_out") == 0) {
        return 0;  // 转账走 transfers 功能，不联动
    }
    if (strcmp(type, "buy") == 0) {
        double v = (qty > 0 && price > 0) ? qty * price : amount;
        return -v;  // 现金流出
    }
    if (strcmp(type, "sell") == 0) {
        double v = (qty > 0 && price > 0) ? qty * price : amount;
        return v;   // 现金流入
    }
    if (strcmp(type, "deposit") == 0 || strcmp(type, "income") == 0) {
        return amount;
    }
    // withdrawal / fee / loss
    return -amount;
}

void transactions_list(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    csilk_db_pool_t* pool = db_get_pool();
    const char* asset_id = csilk_get_query(c, "asset_id");
    const char* category_id = csilk_get_query(c, "category_id");
    const char* type = csilk_get_query(c, "type");
    const char* start_date = csilk_get_query(c, "start_date");
    const char* end_date = csilk_get_query(c, "end_date");

    char sql[1024];
    snprintf(sql, sizeof(sql),
        "SELECT t.id, t.asset_id, t.category_id, t.transaction_type, t.amount, "
        "t.price_per_unit, t.quantity, t.currency, t.transaction_date, t.note, "
        "a.name as asset_name, c.name as category_name "
        "FROM transactions t "
        "LEFT JOIN assets a ON t.asset_id=a.id "
        "LEFT JOIN categories c ON t.category_id=c.id "
        "WHERE t.user_id=%lld", (long long)user_id);

    if (asset_id)
        snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " AND t.asset_id=%s", asset_id);
    if (category_id)
        snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " AND t.category_id=%s", category_id);
    if (type)
        snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " AND t.transaction_type='%s'", type);
    if (start_date)
        snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " AND t.transaction_date >= '%s'", start_date);
    if (end_date)
        snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " AND t.transaction_date <= '%s'", end_date);
    snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " ORDER BY t.transaction_date DESC");

    csilk_json_t* result = csilk_db_query_json(pool, sql);
    if (!result) { respond_error(c, 500, "查询失败"); return; }
    respond_ok(c, result);
}

void transactions_create(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }

    int64_t asset_id = (int64_t)csilk_json_get_number(body, "asset_id");
    int64_t category_id = (int64_t)csilk_json_get_number(body, "category_id");
    const char* type = csilk_json_get_string(body, "transaction_type");
    double amount = csilk_json_get_number(body, "amount");
    const char* date = csilk_json_get_string(body, "transaction_date");

    if (asset_id <= 0 || !type || amount <= 0 || !date) {
        csilk_json_free(body);
        respond_bad_request(c, "asset_id、transaction_type、amount、transaction_date 为必填");
        return;
    }

    csilk_db_pool_t* pool = db_get_pool();

    // Verify asset belongs to user
    char check_sql[256];
    snprintf(check_sql, sizeof(check_sql),
        "SELECT id FROM assets WHERE id=%lld AND user_id=%lld",
        (long long)asset_id, (long long)user_id);
    csilk_json_t* chk = csilk_db_query_json(pool, check_sql);
    if (!chk || csilk_json_array_size(chk) == 0) {
        csilk_json_free(body);
        if (chk) csilk_json_free(chk);
        respond_not_found(c);
        return;
    }
    csilk_json_free(chk);

    const char* currency = csilk_json_get_string(body, "currency");
    if (!currency) currency = "CNY";
    const char* note = csilk_json_get_string(body, "note");
    double price = csilk_json_get_number(body, "price_per_unit");
    double qty = csilk_json_get_number(body, "quantity");

    char sql[512];
    snprintf(sql, sizeof(sql),
        "INSERT INTO transactions (user_id, asset_id, category_id, transaction_type, "
        "amount, price_per_unit, quantity, currency, transaction_date, note) "
        "VALUES (%lld, %lld, %lld, '%s', %.6f, %.4f, %.4f, '%s', '%s', '%s') RETURNING id",
        (long long)user_id, (long long)asset_id, (long long)category_id,
        type, amount, price, qty, currency, date, note ? note : "");

    if (csilk_db_exec(pool, "BEGIN TRANSACTION") != 0) {
        csilk_json_free(body);
        respond_error(c, 500, "数据库错误");
        return;
    }

    csilk_json_t* ins = csilk_db_query_json(pool, sql);
    if (!ins || csilk_json_array_size(ins) == 0) {
        csilk_db_exec(pool, "ROLLBACK");
        if (ins) csilk_json_free(ins);
        csilk_json_free(body);
        respond_error(c, 500, "创建失败");
        return;
    }
    int64_t tx_id = db_get_int(csilk_json_array_get(ins, 0), "id");
    csilk_json_free(ins);

    // 联动资产余额（transfer_* 不联动）
    double tdelta = tx_delta(type, amount, price, qty);
    if (tdelta != 0) {
        if (balance_apply_delta(pool, asset_id, user_id, tdelta,
                                "transaction", tx_id, note) != 0) {
            csilk_db_exec(pool, "ROLLBACK");
            csilk_json_free(body);
            respond_bad_request(c, "资产无效");
            return;
        }
    }

    csilk_db_exec(pool, "COMMIT");
    csilk_json_free(body);
    respond_ok_null(c);
}

void transactions_update(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) { respond_bad_request(c, "缺少 id"); return; }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }

    csilk_db_pool_t* pool = db_get_pool();

    char check_sql[256];
    snprintf(check_sql, sizeof(check_sql),
        "SELECT id FROM transactions WHERE id=%s AND user_id=%lld", id_str, (long long)user_id);
    csilk_json_t* chk = csilk_db_query_json(pool, check_sql);
    if (!chk || csilk_json_array_size(chk) == 0) {
        csilk_json_free(body);
        if (chk) csilk_json_free(chk);
        respond_not_found(c);
        return;
    }
    csilk_json_free(chk);

    // 读取旧记录（差值联动需要；quantity/price 用于重算旧 buy/sell 的 type_delta）
    char old_sql[256];
    snprintf(old_sql, sizeof(old_sql),
        "SELECT asset_id, amount, transaction_type, quantity, price_per_unit "
        "FROM transactions WHERE id=%s AND user_id=%lld", id_str, (long long)user_id);
    csilk_json_t* old_row = csilk_db_query_json(pool, old_sql);
    if (!old_row || csilk_json_array_size(old_row) == 0) {
        csilk_json_free(body);
        if (old_row) csilk_json_free(old_row);
        respond_not_found(c);
        return;
    }
    const csilk_json_t* old_r = csilk_json_array_get(old_row, 0);
    double old_tx_amount = db_get_num(old_r, "amount");
    const char* old_tx_type = csilk_json_get_string(old_r, "transaction_type");
    double old_tx_price = db_get_num(old_r, "price_per_unit");
    double old_tx_qty = db_get_num(old_r, "quantity");
    double old_tdelta = tx_delta(old_tx_type, old_tx_amount, old_tx_price, old_tx_qty);

    const char* type = csilk_json_get_string(body, "transaction_type");
    double amount = csilk_json_get_number(body, "amount");
    const char* date = csilk_json_get_string(body, "transaction_date");
    const char* currency = csilk_json_get_string(body, "currency");
    const char* note = csilk_json_get_string(body, "note");
    double price = csilk_json_get_number(body, "price_per_unit");
    double qty = csilk_json_get_number(body, "quantity");
    int64_t category_id = (int64_t)csilk_json_get_number(body, "category_id");

    char sql[512];
    snprintf(sql, sizeof(sql),
        "UPDATE transactions SET transaction_type='%s', amount=%.6f, price_per_unit=%.4f, "
        "quantity=%.4f, currency='%s', transaction_date='%s', note='%s', "
        "category_id=%lld WHERE id=%s AND user_id=%lld",
        type ? type : "", amount, price, qty,
        currency ? currency : "CNY", date ? date : "", note ? note : "",
        category_id,
        id_str, (long long)user_id);

    if (csilk_db_exec(pool, "BEGIN TRANSACTION") != 0) {
        csilk_json_free(body);
        csilk_json_free(old_row);
        respond_error(c, 500, "数据库错误");
        return;
    }

    csilk_db_exec(pool, sql);

    // 差值联动（transfer_* delta 记 0；非transfer→transfer 时差值=-旧delta 天然回退）
    double new_tdelta = tx_delta(type ? type : "", amount, price, qty);
    double diff = new_tdelta - old_tdelta;
    if (diff != 0) {
        if (balance_apply_delta(pool, db_get_int(old_r, "asset_id"), user_id, diff,
                                "transaction", atoll(id_str), note) != 0) {
            csilk_db_exec(pool, "ROLLBACK");
            csilk_json_free(body);
            csilk_json_free(old_row);
            respond_bad_request(c, "资产无效");
            return;
        }
    }

    csilk_db_exec(pool, "COMMIT");
    csilk_json_free(body);
    csilk_json_free(old_row);
    respond_ok_null(c);
}

void transactions_delete(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) { respond_bad_request(c, "缺少 id"); return; }

    csilk_db_pool_t* pool = db_get_pool();

    // 读取旧记录（反转联动需要）
    char old_sql[256];
    snprintf(old_sql, sizeof(old_sql),
        "SELECT asset_id, amount, transaction_type, quantity, price_per_unit "
        "FROM transactions WHERE id=%s AND user_id=%lld", id_str, (long long)user_id);
    csilk_json_t* old_row = csilk_db_query_json(pool, old_sql);
    if (!old_row || csilk_json_array_size(old_row) == 0) {
        if (old_row) csilk_json_free(old_row);
        respond_not_found(c);
        return;
    }
    const csilk_json_t* old_r = csilk_json_array_get(old_row, 0);
    int64_t asset_id = db_get_int(old_r, "asset_id");
    double old_tdelta = tx_delta(
        csilk_json_get_string(old_r, "transaction_type"),
        db_get_num(old_r, "amount"),
        db_get_num(old_r, "price_per_unit"),
        db_get_num(old_r, "quantity"));

    if (csilk_db_exec(pool, "BEGIN TRANSACTION") != 0) {
        csilk_json_free(old_row);
        respond_error(c, 500, "数据库错误");
        return;
    }

    char sql[256];
    snprintf(sql, sizeof(sql),
        "DELETE FROM transactions WHERE id=%s AND user_id=%lld", id_str, (long long)user_id);
    csilk_db_exec(pool, sql);

    // 反转旧 delta（transfer_* 不联动）
    if (old_tdelta != 0) {
        if (balance_apply_delta(pool, asset_id, user_id, -old_tdelta,
                                "transaction", atoll(id_str), NULL) != 0) {
            csilk_db_exec(pool, "ROLLBACK");
            csilk_json_free(old_row);
            respond_error(c, 500, "删除失败");
            return;
        }
    }

    csilk_db_exec(pool, "COMMIT");
    csilk_json_free(old_row);
    respond_ok_null(c);
}
