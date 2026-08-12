#include "common/response.h"
#include "common/db.h"
#include "common/jwt.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static csilk_json_t* row_to_category(csilk_json_t* row) {
    csilk_json_t* obj = csilk_json_object();
    csilk_json_add_number(obj, "id", db_get_num(row, "id"));
    csilk_json_add_string(obj, "name", csilk_json_get_string(row, "name"));
    csilk_json_add_string(obj, "parent_name", csilk_json_get_string(row, "parent_name"));
    double pid = db_get_num(row, "parent_id");
    csilk_json_add_object(obj, "parent_id",
        pid > 0 ? csilk_json_number(pid) : csilk_json_null());
    const char* type = csilk_json_get_string(row, "type");
    csilk_json_add_string(obj, "type", (type && type[0]) ? type : "asset");
    csilk_json_add_string(obj, "asset_type", csilk_json_get_string(row, "asset_type"));
    csilk_json_add_string(obj, "currency", csilk_json_get_string(row, "currency"));
    csilk_json_add_string(obj, "icon", csilk_json_get_string(row, "icon"));
    csilk_json_add_number(obj, "sort_order", db_get_num(row, "sort_order"));
    return obj;
}

static void add_children(csilk_db_pool_t* pool, csilk_json_t* parent) {
    int64_t pid = db_get_int(parent, "id");
    char sql[256];
    snprintf(sql, sizeof(sql),
        "SELECT c.id, c.name, c.parent_id, "
        "(SELECT p.name FROM categories p WHERE p.id=c.parent_id) as parent_name, "
        "c.type, c.asset_type, c.currency, c.icon, c.sort_order "
        "FROM categories c WHERE parent_id = %lld ORDER BY c.sort_order", (long long)pid);

    csilk_json_t* kids = csilk_db_query_json(pool, sql);
    if (!kids) return;

    size_t n = csilk_json_array_size(kids);
    if (n == 0) { csilk_json_free(kids); return; }

    csilk_json_t* children = csilk_json_array();
    for (size_t i = 0; i < n; i++) {
        csilk_json_t* kid = csilk_json_array_get(kids, i);
        csilk_json_t* kid_obj = row_to_category(kid);
        add_children(pool, kid_obj);
        csilk_json_array_append(children, kid_obj);
    }
    csilk_json_add_array(parent, "children", children);
    csilk_json_free(kids);
}

typedef struct {
    const char* parent_name;
    const char* asset_type;
    int sort_order;
    const char* children[6];
} default_parent_cat_t;

static int64_t insert_cat(csilk_db_pool_t* pool, int64_t user_id, const char* name, int64_t parent_id, const char* type, const char* asset_type, int sort_order) {
    char sql[512];
    if (parent_id > 0) {
        snprintf(sql, sizeof(sql),
            "INSERT INTO categories (user_id, name, parent_id, type, asset_type, currency, icon, sort_order) "
            "VALUES (%lld, '%s', %lld, '%s', '%s', 'CNY', '', %d) RETURNING id",
            (long long)user_id, name, (long long)parent_id, type, asset_type, sort_order);
    } else {
        snprintf(sql, sizeof(sql),
            "INSERT INTO categories (user_id, name, type, asset_type, currency, icon, sort_order) "
            "VALUES (%lld, '%s', '%s', '%s', 'CNY', '', %d) RETURNING id",
            (long long)user_id, name, type, asset_type, sort_order);
    }
    csilk_json_t* res = csilk_db_query_json(pool, sql);
    int64_t id = 0;
    if (res && csilk_json_array_size(res) > 0) {
        id = db_get_int(csilk_json_array_get(res, 0), "id");
    }
    if (res) csilk_json_free(res);
    return id;
}

static void ensure_default_categories_for_type(csilk_db_pool_t* pool, int64_t user_id, const char* type) {
    char count_sql[256];
    snprintf(count_sql, sizeof(count_sql),
        "SELECT COUNT(*) as cnt FROM categories WHERE user_id = %lld AND type = '%s'",
        (long long)user_id, type);
    csilk_json_t* cnt_res = csilk_db_query_json(pool, count_sql);
    if (!cnt_res) return;
    int cnt = 0;
    if (csilk_json_array_size(cnt_res) > 0) {
        cnt = (int)db_get_num(csilk_json_array_get(cnt_res, 0), "cnt");
    }
    csilk_json_free(cnt_res);
    if (cnt > 0) return;

    if (strcmp(type, "expense") == 0) {
        default_parent_cat_t items[] = {
            {"餐饮美食", "cash", 1, {"早晚餐/正餐", "水果零食", "外卖聚餐", NULL}},
            {"交通出行", "cash", 2, {"公共交通", "打车网约车", "加油停车", NULL}},
            {"日常购物", "cash", 3, {"服饰鞋包", "日用百货", "数码家电", NULL}},
            {"居住缴费", "cash", 4, {"房租房贷", "水电燃气", "网络话费", NULL}},
            {"休闲娱乐", "cash", 5, {"游戏影视", "运动健身", "旅游度假", NULL}},
            {"医疗健康", "cash", 6, {"药品诊疗", "保健体检", NULL}}
        };
        for (size_t i = 0; i < sizeof(items)/sizeof(items[0]); i++) {
            int64_t pid = insert_cat(pool, user_id, items[i].parent_name, 0, "expense", items[i].asset_type, items[i].sort_order);
            if (pid > 0) {
                for (int j = 0; items[i].children[j] != NULL; j++) {
                    insert_cat(pool, user_id, items[i].children[j], pid, "expense", items[i].asset_type, j + 1);
                }
            }
        }
    } else if (strcmp(type, "income") == 0) {
        default_parent_cat_t items[] = {
            {"职业收入", "cash", 1, {"基本工资", "绩效奖金", "兼职外包", NULL}},
            {"投资理财", "cash", 2, {"股票/基金收益", "存款利息", "股息分红", NULL}},
            {"其他收入", "cash", 3, {"二手转让", "礼金红包", NULL}}
        };
        for (size_t i = 0; i < sizeof(items)/sizeof(items[0]); i++) {
            int64_t pid = insert_cat(pool, user_id, items[i].parent_name, 0, "income", items[i].asset_type, items[i].sort_order);
            if (pid > 0) {
                for (int j = 0; items[i].children[j] != NULL; j++) {
                    insert_cat(pool, user_id, items[i].children[j], pid, "income", items[i].asset_type, j + 1);
                }
            }
        }
    } else if (strcmp(type, "transaction") == 0) {
        default_parent_cat_t items[] = {
            {"证券交易", "cash", 1, {"股票买卖", "基金申赎", "债券买卖", NULL}},
            {"加密资产", "cash", 2, {"现货买卖", "合约质押", NULL}},
            {"资金调拨", "cash", 3, {"银证/出入金", "存现/取现", "交易手续费", NULL}}
        };
        for (size_t i = 0; i < sizeof(items)/sizeof(items[0]); i++) {
            int64_t pid = insert_cat(pool, user_id, items[i].parent_name, 0, "transaction", items[i].asset_type, items[i].sort_order);
            if (pid > 0) {
                for (int j = 0; items[i].children[j] != NULL; j++) {
                    insert_cat(pool, user_id, items[i].children[j], pid, "transaction", items[i].asset_type, j + 1);
                }
            }
        }
    } else if (strcmp(type, "asset") == 0) {
        default_parent_cat_t items[] = {
            {"流动资产", "cash", 1, {"现金账户", "银行存款", NULL}},
            {"投资资产", "stock", 2, {"股票证券", "基金理财", "加密货币", NULL}},
            {"负债账户", "credit_card", 3, {"信用卡", "房贷/车贷/贷款", NULL}}
        };
        for (size_t i = 0; i < sizeof(items)/sizeof(items[0]); i++) {
            int64_t pid = insert_cat(pool, user_id, items[i].parent_name, 0, "asset", items[i].asset_type, items[i].sort_order);
            if (pid > 0) {
                for (int j = 0; items[i].children[j] != NULL; j++) {
                    const char* child_asset_type = items[i].asset_type;
                    if (strcmp(items[i].children[j], "股票证券") == 0) child_asset_type = "stock";
                    else if (strcmp(items[i].children[j], "基金理财") == 0) child_asset_type = "fund";
                    else if (strcmp(items[i].children[j], "加密货币") == 0) child_asset_type = "crypto";
                    else if (strcmp(items[i].children[j], "房贷/车贷/贷款") == 0) child_asset_type = "loan";
                    else if (strcmp(items[i].children[j], "现金账户") == 0 || strcmp(items[i].children[j], "银行存款") == 0) child_asset_type = "cash";
                    else if (strcmp(items[i].children[j], "信用卡") == 0) child_asset_type = "credit_card";
                    insert_cat(pool, user_id, items[i].children[j], pid, "asset", child_asset_type, j + 1);
                }
            }
        }
    }
}

void categories_list(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    const char* type_query = csilk_get_query(c, "type");
    csilk_db_pool_t* pool = db_get_pool();

    if (type_query && strlen(type_query) > 0) {
        if (strstr(type_query, ",")) {
            char copy[128];
            strncpy(copy, type_query, sizeof(copy)-1);
            copy[sizeof(copy)-1] = '\0';
            char* tok = strtok(copy, ",");
            while (tok) {
                while (*tok == ' ') tok++;
                if (strlen(tok) > 0) {
                    ensure_default_categories_for_type(pool, user_id, tok);
                }
                tok = strtok(NULL, ",");
            }
        } else {
            ensure_default_categories_for_type(pool, user_id, type_query);
        }
    } else {
        ensure_default_categories_for_type(pool, user_id, "asset");
        ensure_default_categories_for_type(pool, user_id, "expense");
        ensure_default_categories_for_type(pool, user_id, "income");
        ensure_default_categories_for_type(pool, user_id, "transaction");
    }

    char sql[512];

    if (type_query && strlen(type_query) > 0) {
        if (strstr(type_query, ",")) {
            char in_clause[256] = {0};
            char copy[128];
            strncpy(copy, type_query, sizeof(copy)-1);
            copy[sizeof(copy)-1] = '\0';
            char* tok = strtok(copy, ",");
            int first = 1;
            while (tok) {
                while (*tok == ' ') tok++;
                if (strlen(tok) > 0) {
                    if (!first) strncat(in_clause, ",", sizeof(in_clause)-strlen(in_clause)-1);
                    strncat(in_clause, "'", sizeof(in_clause)-strlen(in_clause)-1);
                    strncat(in_clause, tok, sizeof(in_clause)-strlen(in_clause)-1);
                    strncat(in_clause, "'", sizeof(in_clause)-strlen(in_clause)-1);
                    first = 0;
                }
                tok = strtok(NULL, ",");
            }
            snprintf(sql, sizeof(sql),
                "SELECT c.id, c.name, c.parent_id, "
                "(SELECT p.name FROM categories p WHERE p.id=c.parent_id) as parent_name, "
                "c.type, c.asset_type, c.currency, c.icon, c.sort_order "
                "FROM categories c WHERE user_id = %lld AND parent_id IS NULL AND c.type IN (%s) ORDER BY c.sort_order",
                (long long)user_id, in_clause[0] ? in_clause : "'asset'");
        } else {
            snprintf(sql, sizeof(sql),
                "SELECT c.id, c.name, c.parent_id, "
                "(SELECT p.name FROM categories p WHERE p.id=c.parent_id) as parent_name, "
                "c.type, c.asset_type, c.currency, c.icon, c.sort_order "
                "FROM categories c WHERE user_id = %lld AND parent_id IS NULL AND c.type = '%s' ORDER BY c.sort_order",
                (long long)user_id, type_query);
        }
    } else {
        snprintf(sql, sizeof(sql),
            "SELECT c.id, c.name, c.parent_id, "
            "(SELECT p.name FROM categories p WHERE p.id=c.parent_id) as parent_name, "
            "c.type, c.asset_type, c.currency, c.icon, c.sort_order "
            "FROM categories c WHERE user_id = %lld AND parent_id IS NULL ORDER BY c.sort_order",
            (long long)user_id);
    }

    csilk_json_t* rows = csilk_db_query_json(pool, sql);
    if (!rows) { respond_error(c, 500, "查询失败"); return; }

    csilk_json_t* tree = csilk_json_array();
    size_t n = csilk_json_array_size(rows);
    for (size_t i = 0; i < n; i++) {
        csilk_json_t* row = csilk_json_array_get(rows, i);
        csilk_json_t* node = row_to_category(row);
        add_children(pool, node);
        csilk_json_array_append(tree, node);
    }
    csilk_json_free(rows);
    respond_ok(c, tree);
}

void categories_create(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }

    const char* name = csilk_json_get_string(body, "name");
    const char* type = csilk_json_get_string(body, "type");
    if (!type || strlen(type) == 0) type = "asset";

    const char* asset_type = csilk_json_get_string(body, "asset_type");
    if (!asset_type || strlen(asset_type) == 0) asset_type = "cash";

    if (!name) {
        csilk_json_free(body);
        respond_bad_request(c, "name 为必填");
        return;
    }

    const char* currency = csilk_json_get_string(body, "currency");
    if (!currency) currency = "CNY";
    const char* icon = csilk_json_get_string(body, "icon");
    if (!icon) icon = "";
    int64_t parent_id = (int64_t)csilk_json_get_number(body, "parent_id");
    int sort_order = (int)csilk_json_get_number(body, "sort_order");

    csilk_db_pool_t* pool = db_get_pool();
    char sql[512];
    if (parent_id > 0) {
        snprintf(sql, sizeof(sql),
            "INSERT INTO categories (user_id, name, parent_id, type, asset_type, currency, icon, sort_order) "
            "VALUES (%lld, '%s', %lld, '%s', '%s', '%s', '%s', %d)",
            (long long)user_id, name, parent_id, type, asset_type, currency, icon, sort_order);
    } else {
        snprintf(sql, sizeof(sql),
            "INSERT INTO categories (user_id, name, type, asset_type, currency, icon, sort_order) "
            "VALUES (%lld, '%s', '%s', '%s', '%s', '%s', %d)",
            (long long)user_id, name, type, asset_type, currency, icon, sort_order);
    }

    if (csilk_db_exec(pool, sql) != 0) {
        csilk_json_free(body);
        respond_error(c, 500, "创建失败");
        return;
    }
    csilk_json_free(body);
    respond_ok_null(c);
}

void categories_update(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) { respond_bad_request(c, "缺少 id"); return; }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }

    const char* name = csilk_json_get_string(body, "name");
    const char* type = csilk_json_get_string(body, "type");
    if (!type || strlen(type) == 0) type = "asset";

    const char* asset_type = csilk_json_get_string(body, "asset_type");
    if (!asset_type || strlen(asset_type) == 0) asset_type = "cash";

    const char* currency = csilk_json_get_string(body, "currency");
    const char* icon = csilk_json_get_string(body, "icon");
    int sort_order = (int)csilk_json_get_number(body, "sort_order");

    char sql[512];
    snprintf(sql, sizeof(sql),
        "UPDATE categories SET name='%s', type='%s', asset_type='%s', currency='%s', icon='%s', sort_order=%d "
        "WHERE id=%s AND user_id=%lld",
        name ? name : "", type, asset_type,
        currency ? currency : "CNY", icon ? icon : "", sort_order,
        id_str, (long long)user_id);

    csilk_db_exec(db_get_pool(), sql);
    csilk_json_free(body);
    respond_ok_null(c);
}

void categories_delete(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) { respond_bad_request(c, "缺少 id"); return; }

    csilk_db_pool_t* pool = db_get_pool();

    // Check if has children
    char sql[256];
    snprintf(sql, sizeof(sql),
        "SELECT COUNT(*) as cnt FROM categories WHERE parent_id = %s AND user_id = %lld",
        id_str, (long long)user_id);
    csilk_json_t* cnt_result = csilk_db_query_json(pool, sql);
    if (cnt_result && csilk_json_array_size(cnt_result) > 0) {
        int cnt = (int)db_get_num(csilk_json_array_get(cnt_result, 0), "cnt");
        csilk_json_free(cnt_result);
        if (cnt > 0) { respond_forbidden(c, "分类下有子分类，无法删除"); return; }
    } else {
        if (cnt_result) csilk_json_free(cnt_result);
    }

    snprintf(sql, sizeof(sql), "DELETE FROM categories WHERE id=%s AND user_id=%lld",
             id_str, (long long)user_id);
    csilk_db_exec(pool, sql);
    respond_ok_null(c);
}
