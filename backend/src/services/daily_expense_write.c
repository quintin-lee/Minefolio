#include "services/daily_expense_write.h"
#include "repositories/daily_expense_repo.h"
#include "common/response.h"
#include "common/ctx.h"
#include "common/db.h"
#include "common/balance.h"
#include "csilk/csilk.h"
#include <string.h>

static int64_t
get_or_create_tag(csilk_db_pool_t* pool, int64_t user_id, const csilk_json_t* tag_obj)
{
    if (!pool || user_id <= 0 || !tag_obj) {
        return 0;
    }
    int64_t     tag_id = db_get_int(tag_obj, "id");
    const char* name = csilk_json_get_string(tag_obj, "name");

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    if (tag_id > 0) {
        char tid_str[32];
        snprintf(tid_str, sizeof(tid_str), "%lld", (long long)tag_id);
        const char*   params[] = {tid_str, uid_str, NULL};
        csilk_json_t* chk =
            csilk_db_query_param_json(pool, "SELECT id FROM tags WHERE id=? AND user_id=?", params);
        if (chk && csilk_json_array_size(chk) > 0) {
            csilk_json_free(chk);
            return tag_id;
        }
        if (chk) {
            csilk_json_free(chk);
        }
    }

    if (name && name[0]) {
        const char*   q_params[] = {uid_str, name, NULL};
        csilk_json_t* q_res = csilk_db_query_param_json(
            pool, "SELECT id FROM tags WHERE user_id=? AND name=?", q_params);
        if (q_res && csilk_json_array_size(q_res) > 0) {
            int64_t existing_id = db_get_int(csilk_json_array_get(q_res, 0), "id");
            csilk_json_free(q_res);
            return existing_id;
        }
        if (q_res) {
            csilk_json_free(q_res);
        }

        const char* color = csilk_json_get_string(tag_obj, "color");
        if (!color || !color[0]) {
            color = "#3b82f6";
        }
        const char*   ins_params[] = {uid_str, name, color, NULL};
        csilk_json_t* ins_res = csilk_db_query_param_json(
            pool,
            "INSERT INTO tags (user_id, name, color) VALUES (?, ?, ?) RETURNING id",
            ins_params);
        if (ins_res && csilk_json_array_size(ins_res) > 0) {
            int64_t new_id = db_get_int(csilk_json_array_get(ins_res, 0), "id");
            csilk_json_free(ins_res);
            return new_id;
        }
        if (ins_res) {
            csilk_json_free(ins_res);
        }
    }

    return 0;
}

void
daily_expenses_create(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }

    int64_t     category_id = db_get_int(body, "category_id");
    int64_t     asset_id = db_get_int(body, "asset_id");
    const char* type = csilk_json_get_string(body, "expense_type");
    double      amount = db_get_num(body, "amount");
    const char* date = csilk_json_get_string(body, "expense_date");

    if (category_id <= 0 || asset_id <= 0 || !type || amount <= 0 || !date) {
        csilk_json_free(body);
        respond_bad_request(c, "asset_id、category_id、expense_type、amount、expense_date 为必填");
        return;
    }

    int is_income = strcmp(type, "income") == 0;
    int is_expense = strcmp(type, "expense") == 0;
    if (!is_income && !is_expense) {
        csilk_json_free(body);
        respond_bad_request(c, "expense_type 必须为 income 或 expense");
        return;
    }

    const char* currency = csilk_json_get_string(body, "currency");
    if (!currency) {
        currency = "CNY";
    }
    const char*   note = csilk_json_get_string(body, "note");
    csilk_json_t* tags = csilk_json_get(body, "tags");

    csilk_db_pool_t* pool = db_get_pool();
    if (csilk_db_exec(pool, "BEGIN TRANSACTION") != 0) {
        csilk_json_free(body);
        respond_error(c, 500, "数据库错误");
        return;
    }

    int64_t expense_id =
        de_insert(pool, user_id, category_id, asset_id, type, amount, currency, date, note);
    if (expense_id <= 0) {
        csilk_db_exec(pool, "ROLLBACK");
        csilk_json_free(body);
        respond_error(c, 500, "创建失败");
        return;
    }

    double business_delta = is_income ? amount : -amount;
    if (balance_apply_delta(
            pool, asset_id, user_id, business_delta, "daily_expense", expense_id, note) != 0) {
        csilk_db_exec(pool, "ROLLBACK");
        csilk_json_free(body);
        respond_bad_request(c, "资产无效");
        return;
    }

    if (tags && csilk_json_is_array(tags)) {
        size_t n = csilk_json_array_size(tags);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* tag_obj = csilk_json_array_get(tags, i);
            int64_t       tag_id = get_or_create_tag(pool, user_id, tag_obj);
            if (tag_id <= 0) {
                continue;
            }
            if (de_tag_insert(pool, expense_id, tag_id) == 0) {
                csilk_db_exec(pool, "ROLLBACK");
                csilk_json_free(body);
                respond_error(c, 500, "创建失败");
                return;
            }
        }
    }

    csilk_db_exec(pool, "COMMIT");
    csilk_json_free(body);
    respond_ok_null(c);
}

void
daily_expenses_update(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) {
        respond_bad_request(c, "缺少 id");
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }

    csilk_db_pool_t* pool = db_get_pool();
    char             uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    const char*   chk_params[] = {id_str, uid_str, NULL};
    csilk_json_t* chk = csilk_db_query_param_json(
        pool, "SELECT id FROM daily_expenses WHERE id=? AND user_id=?", chk_params);
    if (!chk || csilk_json_array_size(chk) == 0) {
        csilk_json_free(body);
        if (chk) {
            csilk_json_free(chk);
        }
        respond_not_found(c);
        return;
    }
    csilk_json_free(chk);

    csilk_json_t* old_row = de_get(pool, user_id, atoll(id_str));
    if (!old_row || csilk_json_array_size(old_row) == 0) {
        csilk_json_free(body);
        if (old_row) {
            csilk_json_free(old_row);
        }
        respond_not_found(c);
        return;
    }
    const csilk_json_t* old_r = csilk_json_array_get(old_row, 0);
    double              old_amount = db_get_num(old_r, "amount");
    const char*         old_type = csilk_json_get_string(old_r, "expense_type");
    int64_t             old_asset_id = db_get_int(old_r, "asset_id");
    double old_delta = (old_type && strcmp(old_type, "income") == 0) ? old_amount : -old_amount;

    int64_t       category_id = db_get_int(body, "category_id");
    int64_t       asset_id = db_get_int(body, "asset_id");
    const char*   type = csilk_json_get_string(body, "expense_type");
    double        amount = db_get_num(body, "amount");
    const char*   date = csilk_json_get_string(body, "expense_date");
    const char*   currency = csilk_json_get_string(body, "currency");
    const char*   note = csilk_json_get_string(body, "note");
    csilk_json_t* tags = csilk_json_get(body, "tags");

    if (category_id <= 0 || asset_id <= 0 || !type || amount <= 0 || !date) {
        csilk_json_free(body);
        csilk_json_free(old_row);
        respond_bad_request(c, "asset_id、category_id、expense_type、amount、expense_date 为必填");
        return;
    }

    int is_income = strcmp(type, "income") == 0;
    int is_expense = strcmp(type, "expense") == 0;
    if (!is_income && !is_expense) {
        csilk_json_free(body);
        csilk_json_free(old_row);
        respond_bad_request(c, "expense_type 必须为 income 或 expense");
        return;
    }

    if (csilk_db_exec(pool, "BEGIN TRANSACTION") != 0) {
        csilk_json_free(body);
        csilk_json_free(old_row);
        respond_error(c, 500, "数据库错误");
        return;
    }

    if (!de_update(pool,
                   user_id,
                   atoll(id_str),
                   category_id,
                   asset_id,
                   type ? type : "",
                   amount,
                   currency ? currency : "CNY",
                   date ? date : "",
                   note ? note : "")) {
        csilk_db_exec(pool, "ROLLBACK");
        csilk_json_free(body);
        csilk_json_free(old_row);
        respond_error(c, 500, "更新失败");
        return;
    }

    double new_delta = is_income ? amount : -amount;
    if (asset_id == old_asset_id) {
        if (new_delta != old_delta) {
            if (balance_apply_delta(pool,
                                    asset_id,
                                    user_id,
                                    new_delta - old_delta,
                                    "daily_expense",
                                    atoll(id_str),
                                    note) != 0) {
                csilk_db_exec(pool, "ROLLBACK");
                csilk_json_free(body);
                csilk_json_free(old_row);
                respond_bad_request(c, "资产无效");
                return;
            }
        }
    } else {
        if (balance_apply_delta(
                pool, old_asset_id, user_id, -old_delta, "daily_expense", atoll(id_str), note) !=
                0 ||
            balance_apply_delta(
                pool, asset_id, user_id, new_delta, "daily_expense", atoll(id_str), note) != 0) {
            csilk_db_exec(pool, "ROLLBACK");
            csilk_json_free(body);
            csilk_json_free(old_row);
            respond_bad_request(c, "资产无效");
            return;
        }
    }

    if (de_tag_delete_all(pool, atoll(id_str)) == 0) {
        csilk_db_exec(pool, "ROLLBACK");
        csilk_json_free(body);
        csilk_json_free(old_row);
        respond_error(c, 500, "更新失败");
        return;
    }
    if (tags && csilk_json_is_array(tags)) {
        size_t n = csilk_json_array_size(tags);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* tag_obj = csilk_json_array_get(tags, i);
            int64_t       tag_id = get_or_create_tag(pool, user_id, tag_obj);
            if (tag_id <= 0) {
                continue;
            }
            if (de_tag_insert(pool, atoll(id_str), tag_id) == 0) {
                csilk_db_exec(pool, "ROLLBACK");
                csilk_json_free(body);
                csilk_json_free(old_row);
                respond_error(c, 500, "更新失败");
                return;
            }
        }
    }

    csilk_db_exec(pool, "COMMIT");
    csilk_json_free(body);
    csilk_json_free(old_row);
    respond_ok_null(c);
}

void
daily_expenses_delete(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) {
        respond_bad_request(c, "缺少 id");
        return;
    }

    csilk_db_pool_t* pool = db_get_pool();
    char             uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    csilk_json_t* old_row = de_get(pool, user_id, atoll(id_str));
    if (!old_row || csilk_json_array_size(old_row) == 0) {
        if (old_row) {
            csilk_json_free(old_row);
        }
        respond_not_found(c);
        return;
    }
    const csilk_json_t* old_r = csilk_json_array_get(old_row, 0);
    double              old_amount = db_get_num(old_r, "amount");
    const char*         old_type = csilk_json_get_string(old_r, "expense_type");
    int64_t             asset_id = db_get_int(old_r, "asset_id");
    double old_delta = (old_type && strcmp(old_type, "income") == 0) ? old_amount : -old_amount;

    if (csilk_db_exec(pool, "BEGIN TRANSACTION") != 0) {
        csilk_json_free(old_row);
        respond_error(c, 500, "数据库错误");
        return;
    }

    if (de_tag_delete_all(pool, atoll(id_str)) == 0) {
        csilk_db_exec(pool, "ROLLBACK");
        csilk_json_free(old_row);
        respond_error(c, 500, "删除失败");
        return;
    }
    if (!de_delete(pool, user_id, atoll(id_str))) {
        csilk_db_exec(pool, "ROLLBACK");
        csilk_json_free(old_row);
        respond_error(c, 500, "删除失败");
        return;
    }

    if (old_delta != 0) {
        if (balance_apply_delta(
                pool, asset_id, user_id, -old_delta, "daily_expense", atoll(id_str), NULL) != 0) {
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
