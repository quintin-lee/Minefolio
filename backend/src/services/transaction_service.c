#include "services/transaction_service.h"
#include "common/response.h"
#include "common/db.h"
#include "common/jwt.h"
#include "common/balance.h"
#include "common/tx_types.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void transactions_list(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    int64_t page = 1, page_size = 20;
    parse_page_params(c, &page, &page_size);

    csilk_db_pool_t* pool = db_get_pool();
    const char* asset_id = csilk_get_query(c, "asset_id");
    const char* category_id = csilk_get_query(c, "category_id");
    const char* type = csilk_get_query(c, "type");
    const char* start_date = csilk_get_query(c, "start_date");
    const char* end_date = csilk_get_query(c, "end_date");
    const char* source_type = csilk_get_query(c, "source_type");

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    char sql[1024];
    snprintf(sql, sizeof(sql),
        "SELECT t.id, t.asset_id, t.linked_asset_id, t.category_id, t.transaction_type, t.source_type, "
        "t.direction, t.linked_direction, t.amount, "
        "t.price_per_unit, t.quantity, t.currency, t.transaction_date, t.note, "
        "a.name as asset_name, la.name as linked_asset_name, c.name as category_name "
        "FROM transactions t "
        "LEFT JOIN assets a ON t.asset_id=a.id "
        "LEFT JOIN assets la ON t.linked_asset_id=la.id "
        "LEFT JOIN categories c ON t.category_id=c.id "
        "WHERE t.user_id=?");

    char count_sql[1024];
    snprintf(count_sql, sizeof(count_sql),
        "SELECT COUNT(*) AS cnt FROM transactions t WHERE t.user_id=?");

    const char* params[16];
    params[0] = uid_str;
    int pidx = 1;

    if (asset_id) {
        snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " AND t.asset_id=?");
        snprintf(count_sql + strlen(count_sql), sizeof(count_sql) - strlen(count_sql), " AND t.asset_id=?");
        params[pidx++] = asset_id;
    }
    if (category_id) {
        snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " AND t.category_id=?");
        snprintf(count_sql + strlen(count_sql), sizeof(count_sql) - strlen(count_sql), " AND t.category_id=?");
        params[pidx++] = category_id;
    }
    if (type) {
        snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " AND t.transaction_type=?");
        snprintf(count_sql + strlen(count_sql), sizeof(count_sql) - strlen(count_sql), " AND t.transaction_type=?");
        params[pidx++] = type;
    }
    if (source_type) {
        snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " AND t.source_type=?");
        snprintf(count_sql + strlen(count_sql), sizeof(count_sql) - strlen(count_sql), " AND t.source_type=?");
        params[pidx++] = source_type;
    }
    if (start_date) {
        snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " AND t.transaction_date >= ?");
        snprintf(count_sql + strlen(count_sql), sizeof(count_sql) - strlen(count_sql), " AND t.transaction_date >= ?");
        params[pidx++] = start_date;
    }
    if (end_date) {
        snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " AND t.transaction_date <= ?");
        snprintf(count_sql + strlen(count_sql), sizeof(count_sql) - strlen(count_sql), " AND t.transaction_date <= ?");
        params[pidx++] = end_date;
    }
    snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " ORDER BY t.transaction_date DESC");

    int pidx_count = pidx; // count query ends before pagination params

    char limit_buf[32], offset_buf[32];
    snprintf(limit_buf, sizeof(limit_buf), "%lld", (long long)page_size);
    snprintf(offset_buf, sizeof(offset_buf), "%lld", (long long)((page - 1) * page_size));
    snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " LIMIT ? OFFSET ?");
    params[pidx++] = limit_buf;
    params[pidx++] = offset_buf;
    params[pidx] = NULL;

    params[pidx_count] = NULL;
    csilk_json_t* cnt_res = csilk_db_query_param_json(pool, count_sql, params);
    int64_t total = 0;
    if (cnt_res && csilk_json_array_size(cnt_res) > 0) {
        total = db_get_int(csilk_json_array_get(cnt_res, 0), "cnt");
    }
    if (cnt_res) csilk_json_free(cnt_res);
    params[pidx_count] = limit_buf;

    csilk_json_t* result = csilk_db_query_param_json(pool, sql, params);
    if (!result) { respond_error(c, 500, "查询失败"); return; }
    respond_page_ok(c, result, total, page, page_size);
}

void transactions_monthly(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    const char* month = csilk_get_query(c, "month");
    if (!month || strlen(month) != 7 || month[4] != '-') {
        respond_bad_request(c, "month 参数格式错误 (YYYY-MM)");
        return;
    }
    for (int i = 0; i < 7; i++) {
        if (i != 4 && (month[i] < '0' || month[i] > '9')) {
            respond_bad_request(c, "month 参数格式错误 (YYYY-MM)");
            return;
        }
    }

    char uid_str[32], pattern[16];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(pattern, sizeof(pattern), "%.4s-%.2s%%", month, month + 5);

    const char* params[] = { uid_str, pattern, NULL };
    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t* res = csilk_db_query_param_json(pool,
        "SELECT "
        "  COALESCE(SUM(amount), 0) AS total_volume, "
        "  COALESCE(SUM(CASE WHEN direction='in' THEN amount ELSE 0 END), 0) AS inflows, "
        "  COALESCE(SUM(CASE WHEN direction='out' THEN amount ELSE 0 END), 0) AS outflows, "
        "  COUNT(*) AS count "
        "FROM transactions WHERE user_id=? AND transaction_date LIKE ?", params);
    if (!res) { respond_error(c, 500, "查询失败"); return; }

    csilk_json_t* resp = csilk_json_object();
    if (csilk_json_array_size(res) > 0) {
        const csilk_json_t* row = csilk_json_array_get(res, 0);
        csilk_json_add_number(resp, "total_volume", db_get_num(row, "total_volume"));
        csilk_json_add_number(resp, "inflows", db_get_num(row, "inflows"));
        csilk_json_add_number(resp, "outflows", db_get_num(row, "outflows"));
        csilk_json_add_number(resp, "count", db_get_num(row, "count"));
    } else {
        csilk_json_add_number(resp, "total_volume", 0);
        csilk_json_add_number(resp, "inflows", 0);
        csilk_json_add_number(resp, "outflows", 0);
        csilk_json_add_number(resp, "count", 0);
    }
    csilk_json_free(res);
    respond_ok(c, resp);
}

void transactions_create(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }

    int64_t asset_id = db_get_int(body, "asset_id");
    int64_t linked_asset_id = db_get_int(body, "linked_asset_id");
    int64_t category_id = db_get_int(body, "category_id");
    const char* type = csilk_json_get_string(body, "transaction_type");
    if (!type || strlen(type) == 0) type = csilk_json_get_string(body, "type");
    const char* src_type = csilk_json_get_string(body, "source_type");
    if (!src_type) src_type = "expense";
    double amount = db_get_num(body, "amount");
    const char* date = csilk_json_get_string(body, "transaction_date");

    const tx_type_t* ttype = tx_type_lookup(type ? type : "");
    if (!ttype) {
        csilk_json_free(body);
        respond_bad_request(c, "未知交易类型");
        return;
    }

    if (strcmp(src_type, "income") != 0 && strcmp(src_type, "expense") != 0) {
        csilk_json_free(body);
        respond_bad_request(c, "source_type 必须为 income 或 expense");
        return;
    }

    if (asset_id <= 0 || !type || amount <= 0 || !date) {
        csilk_json_free(body);
        respond_bad_request(c, "asset_id、transaction_type、amount、transaction_date 为必填");
        return;
    }

    if (linked_asset_id > 0 && linked_asset_id == asset_id) {
        csilk_json_free(body);
        respond_bad_request(c, "关联资金账户不能与投资目标资产相同");
        return;
    }

    csilk_db_pool_t* pool = db_get_pool();
    char uid_str[32], ast_str[32], last_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(ast_str, sizeof(ast_str), "%lld", (long long)asset_id);
    snprintf(last_str, sizeof(last_str), "%lld", (long long)linked_asset_id);

    // Verify main asset belongs to user
    const char* chk_params[] = { ast_str, uid_str, NULL };
    csilk_json_t* chk = csilk_db_query_param_json(pool,
        "SELECT id FROM assets WHERE id=? AND user_id=?", chk_params);
    if (!chk || csilk_json_array_size(chk) == 0) {
        csilk_json_free(body);
        if (chk) csilk_json_free(chk);
        respond_not_found(c);
        return;
    }
    csilk_json_free(chk);

    // Verify linked asset belongs to user if specified
    if (linked_asset_id > 0) {
        const char* lchk_params[] = { last_str, uid_str, NULL };
        csilk_json_t* lchk = csilk_db_query_param_json(pool,
            "SELECT id FROM assets WHERE id=? AND user_id=?", lchk_params);
        if (!lchk || csilk_json_array_size(lchk) == 0) {
            csilk_json_free(body);
            if (lchk) csilk_json_free(lchk);
            respond_bad_request(c, "关联资金账户无效");
            return;
        }
        csilk_json_free(lchk);
    }

    const char* currency = csilk_json_get_string(body, "currency");
    if (!currency) currency = "CNY";
    const char* note = csilk_json_get_string(body, "note");
    double price = db_get_num(body, "price_per_unit");
    double qty = db_get_num(body, "quantity");
    double fee = db_get_num(body, "fee");

    char cat_str[32], amt_str[64], price_str[64], qty_str[64];
    snprintf(cat_str, sizeof(cat_str), "%lld", (long long)category_id);
    snprintf(amt_str, sizeof(amt_str), "%.6f", amount);
    snprintf(price_str, sizeof(price_str), "%.4f", price);
    snprintf(qty_str, sizeof(qty_str), "%.4f", qty);

    const char* ins_params[] = {
        uid_str, ast_str, last_str, cat_str, src_type, type,
        ttype->stat_dir, ttype->linked_dir,
        amt_str, price_str, qty_str, currency, date, note ? note : "", NULL
    };

    if (csilk_db_exec(pool, "BEGIN TRANSACTION") != 0) {
        csilk_json_free(body);
        respond_error(c, 500, "数据库错误");
        return;
    }

    char fee_str[64];
    snprintf(fee_str, sizeof(fee_str), "%.6f", fee);
    const char* ins_params_full[] = {
        uid_str, ast_str, last_str, cat_str, src_type, type,
        ttype->stat_dir, ttype->linked_dir,
        amt_str, price_str, qty_str, fee_str, currency, date, note ? note : "", NULL
    };

    csilk_json_t* ins = csilk_db_query_param_json(pool,
        "INSERT INTO transactions (user_id, asset_id, linked_asset_id, category_id, source_type, transaction_type, "
        "direction, linked_direction, "
        "amount, price_per_unit, quantity, fee, currency, transaction_date, note) "
        "VALUES (?, ?, NULLIF(?, '0'), ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) RETURNING id", ins_params_full);
    if (!ins || csilk_json_array_size(ins) == 0) {
        csilk_db_exec(pool, "ROLLBACK");
        if (ins) csilk_json_free(ins);
        csilk_json_free(body);
        respond_error(c, 500, "创建失败");
        return;
    }
    int64_t tx_id = db_get_int(csilk_json_array_get(ins, 0), "id");
    csilk_json_free(ins);

    // 持仓联动：仅 buy/sell 且投资类资产
    double position_delta = 0;
    int is_investment_tx = 0;
    if (strcmp(type, "buy") == 0 || strcmp(type, "sell") == 0) {
        int pos_rc = apply_position(pool, asset_id, type, amount, fee, price, qty, &position_delta);
        if (pos_rc < 0) {
            csilk_db_exec(pool, "ROLLBACK");
            csilk_json_free(body);
            respond_bad_request(c, "持有份额不足");
            return;
        }
        is_investment_tx = (position_delta != 0 || strcmp(type, "buy") == 0);
    }

    // 手续费：同事务生成 fee 行（仅投资类 buy/sell）
    if (fee > 0 && linked_asset_id > 0 && is_investment_tx) {
        char fee_amt_str[64];
        snprintf(fee_amt_str, sizeof(fee_amt_str), "%.6f", fee);
        char fee_sql[512];
        snprintf(fee_sql, sizeof(fee_sql),
            "INSERT INTO transactions (user_id, asset_id, linked_asset_id, category_id, "
            "source_type, transaction_type, direction, linked_direction, "
            "amount, price_per_unit, quantity, currency, transaction_date, note) "
            "VALUES (%lld, %lld, NULLIF('%lld', '0'), %lld, '%s', 'fee', "
            "'out', NULL, '%s', '0.0000', '0.0000', '%s', '%s', '%s')",
            (long long)user_id, (long long)asset_id, (long long)linked_asset_id,
            (long long)category_id, src_type, fee_amt_str, currency, date,
            note && strlen(note) > 0 ? note : "fee");
        csilk_db_exec(pool, fee_sql);
        if (balance_apply_delta(pool, linked_asset_id, user_id, -fee,
                                "transaction_fee", tx_id, note) != 0) {
            csilk_db_exec(pool, "ROLLBACK");
            csilk_json_free(body);
            respond_bad_request(c, "手续费扣减失败");
            return;
        }
    }

    // 目标资产余额联动（非投资类走原逻辑，投资类已在 apply_position 内处理）
    double tdelta = is_investment_tx ? 0 : tx_delta(type, amount, price, qty);
    if (is_investment_tx) {
        // 投资类：buy 更新 fund current_value（delta=quantity*price-old_current）
        //       sell 也更新 fund current_value（delta=new_qty*net_value-old_current）
        //       净值更新时 assets/update 会重算 current_value
        balance_apply_delta(pool, asset_id, user_id, position_delta,
                            "transaction", tx_id, note);
    } else if (tdelta != 0) {
        if (balance_apply_delta(pool, asset_id, user_id, tdelta,
                                "transaction", tx_id, note) != 0) {
            csilk_db_exec(pool, "ROLLBACK");
            csilk_json_free(body);
            respond_bad_request(c, "资产无效");
            return;
        }
    }

    // 关联资金账户余额联动
    if (linked_asset_id > 0) {
        double ldelta = tx_effective_ldelta(type, amount, tdelta);
        if (ldelta != 0) {
            if (balance_apply_delta(pool, linked_asset_id, user_id, ldelta,
                                    "transaction_linked", tx_id, note) != 0) {
                csilk_db_exec(pool, "ROLLBACK");
                csilk_json_free(body);
                respond_bad_request(c, "关联资金账户余额更新失败");
                return;
            }
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
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    const char* chk_params[] = { id_str, uid_str, NULL };
    csilk_json_t* chk = csilk_db_query_param_json(pool,
        "SELECT id FROM transactions WHERE id=? AND user_id=?", chk_params);
    if (!chk || csilk_json_array_size(chk) == 0) {
        csilk_json_free(body);
        if (chk) csilk_json_free(chk);
        respond_not_found(c);
        return;
    }
    csilk_json_free(chk);

    // 读取旧记录
    const char* old_params[] = { id_str, uid_str, NULL };
    csilk_json_t* old_row = csilk_db_query_param_json(pool,
        "SELECT asset_id, linked_asset_id, amount, transaction_type, quantity, price_per_unit, fee "
        "FROM transactions WHERE id=? AND user_id=?", old_params);
    if (!old_row || csilk_json_array_size(old_row) == 0) {
        csilk_json_free(body);
        if (old_row) csilk_json_free(old_row);
        respond_not_found(c);
        return;
    }
    const csilk_json_t* old_r = csilk_json_array_get(old_row, 0);
    int64_t old_asset_id = db_get_int(old_r, "asset_id");
    int64_t old_linked_asset_id = db_get_int(old_r, "linked_asset_id");
    double old_tx_amount = db_get_num(old_r, "amount");
    const char* old_tx_type = csilk_json_get_string(old_r, "transaction_type");
    double old_tx_price = db_get_num(old_r, "price_per_unit");
    double old_tx_qty = db_get_num(old_r, "quantity");
    double old_fee = db_get_num(old_r, "fee");
    double old_tdelta = tx_delta(old_tx_type, old_tx_amount, old_tx_price, old_tx_qty);
    double old_ldelta = tx_effective_ldelta(old_tx_type, old_tx_amount, old_tdelta);

    int64_t linked_asset_id = db_get_int(body, "linked_asset_id");
    const char* type = csilk_json_get_string(body, "transaction_type");
    if (!type || strlen(type) == 0) type = csilk_json_get_string(body, "type");

    const tx_type_t* ntype = tx_type_lookup(type ? type : "");
    if (!ntype) {
        csilk_json_free(body);
        csilk_json_free(old_row);
        respond_bad_request(c, "未知交易类型");
        return;
    }

    double amount = db_get_num(body, "amount");
    const char* date = csilk_json_get_string(body, "transaction_date");
    const char* currency = csilk_json_get_string(body, "currency");
    const char* note = csilk_json_get_string(body, "note");
    const char* src_type = csilk_json_get_string(body, "source_type");
    if (src_type && strcmp(src_type, "income") != 0 && strcmp(src_type, "expense") != 0) {
        csilk_json_free(body);
        csilk_json_free(old_row);
        respond_bad_request(c, "source_type 必须为 income 或 expense");
        return;
    }
    double price = db_get_num(body, "price_per_unit");
    double qty = db_get_num(body, "quantity");
    int64_t category_id = db_get_int(body, "category_id");
    double fee = db_get_num(body, "fee");

    char amt_str[64], price_str[64], qty_str[64], cat_str[32], last_str[32];
    snprintf(amt_str, sizeof(amt_str), "%.6f", amount);
    snprintf(price_str, sizeof(price_str), "%.4f", price);
    snprintf(qty_str, sizeof(qty_str), "%.4f", qty);
    snprintf(cat_str, sizeof(cat_str), "%lld", (long long)category_id);
    snprintf(last_str, sizeof(last_str), "%lld", (long long)linked_asset_id);

    const char* up_params[] = {
        type ? type : "", ntype->stat_dir, ntype->linked_dir,
        amt_str, price_str, qty_str,
        currency ? currency : "CNY", date ? date : "", note ? note : "",
        cat_str, src_type ? src_type : "expense", last_str,
        id_str, uid_str, NULL
    };

    if (csilk_db_exec(pool, "BEGIN TRANSACTION") != 0) {
        csilk_json_free(body);
        csilk_json_free(old_row);
        respond_error(c, 500, "数据库错误");
        return;
    }

    csilk_json_t* up_res = csilk_db_query_param_json(pool,
        "UPDATE transactions SET transaction_type=?, direction=?, linked_direction=?, amount=?, price_per_unit=?, "
        "quantity=?, currency=?, transaction_date=?, note=?, "
        "category_id=?, source_type=?, linked_asset_id=NULLIF(?, '0') WHERE id=? AND user_id=?", up_params);
    if (up_res) csilk_json_free(up_res);

    // 1. 目标资产：投资类先回滚旧持仓再正推新持仓；非投资类走 diff
    int old_is_invest = (strcmp(old_tx_type, "buy") == 0 || strcmp(old_tx_type, "sell") == 0);
    int new_is_invest = (strcmp(type ? type : "", "buy") == 0 || strcmp(type ? type : "", "sell") == 0);
    double new_tdelta = tx_delta(type ? type : "", amount, price, qty);
    if (old_is_invest) {
        double old_pos_delta = 0;
        apply_position(pool, old_asset_id, old_tx_type, old_tx_amount, old_fee,
                       old_tx_price, old_tx_qty, &old_pos_delta);
    }
     if (new_is_invest) {
         double new_pos_delta = 0;
         int new_rc = apply_position(pool, old_asset_id, type ? type : "", amount, fee,
                                     price, qty, &new_pos_delta);
         if (new_rc < 0) {
             csilk_db_exec(pool, "ROLLBACK");
             csilk_json_free(body);
             csilk_json_free(old_row);
             respond_bad_request(c, "持有份额不足");
             return;
         }
        if (new_pos_delta != 0) {
            if (balance_apply_delta(pool, old_asset_id, user_id, new_pos_delta,
                                    "transaction", atoll(id_str), note) != 0) {
                csilk_db_exec(pool, "ROLLBACK");
                csilk_json_free(body);
                csilk_json_free(old_row);
                respond_bad_request(c, "资产无效");
                return;
            }
        }
    } else {
        double diff = new_tdelta - old_tdelta;
        if (diff != 0) {
            if (balance_apply_delta(pool, old_asset_id, user_id, diff,
                                    "transaction", atoll(id_str), note) != 0) {
                csilk_db_exec(pool, "ROLLBACK");
                csilk_json_free(body);
                csilk_json_free(old_row);
                respond_bad_request(c, "资产无效");
                return;
            }
        }
    }

    // 2. 关联资金账户差值联动
    double new_ldelta = tx_effective_ldelta(type ? type : "", amount, new_tdelta);
    if (linked_asset_id == old_linked_asset_id) {
        if (linked_asset_id > 0) {
            double ldiff = new_ldelta - old_ldelta;
            if (ldiff != 0) {
                if (balance_apply_delta(pool, linked_asset_id, user_id, ldiff,
                                        "transaction_linked", atoll(id_str), note) != 0) {
                    csilk_db_exec(pool, "ROLLBACK");
                    csilk_json_free(body);
                    csilk_json_free(old_row);
                    respond_bad_request(c, "关联资金账户更新失败");
                    return;
                }
            }
        }
    } else {
        if (old_linked_asset_id > 0 && old_ldelta != 0) {
            if (balance_apply_delta(pool, old_linked_asset_id, user_id, -old_ldelta,
                                    "transaction_linked", atoll(id_str), note) != 0) {
                csilk_db_exec(pool, "ROLLBACK");
                csilk_json_free(body);
                csilk_json_free(old_row);
                respond_bad_request(c, "原资金账户更新失败");
                return;
            }
        }
        if (linked_asset_id > 0 && new_ldelta != 0) {
            if (balance_apply_delta(pool, linked_asset_id, user_id, new_ldelta,
                                    "transaction_linked", atoll(id_str), note) != 0) {
                csilk_db_exec(pool, "ROLLBACK");
                csilk_json_free(body);
                csilk_json_free(old_row);
                respond_bad_request(c, "新资金账户更新失败");
                return;
            }
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
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    // 读取旧记录
    const char* old_params[] = { id_str, uid_str, NULL };
    csilk_json_t* old_row = csilk_db_query_param_json(pool,
        "SELECT asset_id, linked_asset_id, amount, transaction_type, quantity, price_per_unit "
        "FROM transactions WHERE id=? AND user_id=?", old_params);
    if (!old_row || csilk_json_array_size(old_row) == 0) {
        if (old_row) csilk_json_free(old_row);
        respond_not_found(c);
        return;
    }
    const csilk_json_t* old_r = csilk_json_array_get(old_row, 0);
    int64_t asset_id = db_get_int(old_r, "asset_id");
    int64_t linked_asset_id = db_get_int(old_r, "linked_asset_id");
    const char* old_tx_type = csilk_json_get_string(old_r, "transaction_type");
    double old_tx_amount = db_get_num(old_r, "amount");
    double old_tx_price = db_get_num(old_r, "price_per_unit");
    double old_tx_qty = db_get_num(old_r, "quantity");
    double old_fee = db_get_num(old_r, "fee");
    double old_tdelta = tx_delta(old_tx_type, old_tx_amount, old_tx_price, old_tx_qty);
    double old_ldelta = tx_effective_ldelta(old_tx_type, old_tx_amount, old_tdelta);

    if (csilk_db_exec(pool, "BEGIN TRANSACTION") != 0) {
        csilk_json_free(old_row);
        respond_error(c, 500, "数据库错误");
        return;
    }

    const char* del_params[] = { id_str, uid_str, NULL };
    csilk_json_t* del_res = csilk_db_query_param_json(pool,
        "DELETE FROM transactions WHERE id=? AND user_id=?", del_params);
    if (del_res) csilk_json_free(del_res);

    // 投资类：回滚持仓
    if (strcmp(old_tx_type, "buy") == 0 || strcmp(old_tx_type, "sell") == 0) {
        apply_position(pool, asset_id, old_tx_type, old_tx_amount, old_fee,
                       old_tx_price, old_tx_qty, NULL);
    }

    // 1. 反转目标资产旧 delta（投资类已由 apply_position 处理，非投资类走此处）
    if (old_tdelta != 0 && !(strcmp(old_tx_type, "buy") == 0 || strcmp(old_tx_type, "sell") == 0)) {
        if (balance_apply_delta(pool, asset_id, user_id, -old_tdelta,
                                "transaction", atoll(id_str), NULL) != 0) {
            csilk_db_exec(pool, "ROLLBACK");
            csilk_json_free(old_row);
            respond_error(c, 500, "删除失败");
            return;
        }
    }

    // 2. 反转关联资金账户旧 delta
    if (linked_asset_id > 0 && old_ldelta != 0) {
        if (balance_apply_delta(pool, linked_asset_id, user_id, -old_ldelta,
                                "transaction_linked", atoll(id_str), NULL) != 0) {
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
void register_transaction_routes(csilk_app_t* app) {
    csilk_app_get_ext(app, "/api/transactions", transactions_list, nullptr, "transaction_resp_t", "List transactions", "Returns paginated transaction list with optional filters");
    csilk_app_get_ext(app, "/api/transactions/monthly", transactions_monthly, nullptr, nullptr, "Monthly transaction summary", "Returns monthly aggregated transaction totals");
    csilk_app_post_ext(app, "/api/transactions", transactions_create, "transaction_req_t", "transaction_resp_t", "Create transaction", "Create a new transaction (expense, income, transfer, investment buy/sell)");
    csilk_app_put_ext(app, "/api/transactions/:id", transactions_update, "transaction_req_t", "transaction_resp_t", "Update transaction", "Update an existing transaction by ID");
    csilk_app_delete_ext(app, "/api/transactions/:id", transactions_delete, nullptr, nullptr, "Delete transaction", "Delete a transaction by ID");
}
