#include "repositories/import_rule_repo.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

csilk_json_t*
import_rule_list(csilk_db_pool_t* pool, int64_t user_id)
{
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    return csilk_db_query_param_json(
        pool,
        "SELECT r.id, r.user_id, r.keyword, r.match_field, r.match_type, "
        "r.category_id, r.target_type, r.priority, r.is_active, r.created_at, "
        "c.name as category_name "
        "FROM import_rules r "
        "LEFT JOIN categories c ON r.category_id = c.id "
        "WHERE r.user_id = ? "
        "ORDER BY r.priority ASC, r.id ASC",
        (const char*[]){uid, NULL});
}

csilk_json_t*
import_rule_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id)
{
    char uid[32], rid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(rid, sizeof(rid), "%lld", (long long)id);
    return csilk_db_query_param_json(
        pool,
        "SELECT r.id, r.user_id, r.keyword, r.match_field, r.match_type, "
        "r.category_id, r.target_type, r.priority, r.is_active, r.created_at, "
        "c.name as category_name "
        "FROM import_rules r "
        "LEFT JOIN categories c ON r.category_id = c.id "
        "WHERE r.user_id = ? AND r.id = ?",
        (const char*[]){uid, rid, NULL});
}

int64_t
import_rule_insert(csilk_db_pool_t* pool,
                   int64_t          user_id,
                   const char*      keyword,
                   const char*      match_field,
                   const char*      match_type,
                   int64_t          category_id,
                   const char*      target_type,
                   int              priority,
                   int              is_active)
{
    char uid[32], cid[32], prio[32], act[16];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(cid, sizeof(cid), "%lld", (long long)category_id);
    snprintf(prio, sizeof(prio), "%d", priority);
    snprintf(act, sizeof(act), "%d", is_active ? 1 : 0);

    const char* cid_param = (category_id > 0) ? cid : NULL;

    csilk_json_t* res =
        csilk_db_query_param_json(pool,
                                  "INSERT INTO import_rules (user_id, keyword, match_field, "
                                  "match_type, category_id, target_type, priority, is_active) "
                                  "VALUES (?, ?, ?, ?, ?, ?, ?, ?) RETURNING id",
                                  (const char*[]){uid,
                                                  keyword,
                                                  match_field ? match_field : "all",
                                                  match_type ? match_type : "contains",
                                                  cid_param,
                                                  target_type ? target_type : "expense",
                                                  prio,
                                                  act,
                                                  NULL});

    int64_t new_id = 0;
    if (res && csilk_json_array_size(res) > 0) {
        new_id = db_get_int(csilk_json_array_get(res, 0), "id");
    }
    if (res) {
        csilk_json_free(res);
    }
    return new_id;
}

int
import_rule_update(csilk_db_pool_t* pool,
                   int64_t          user_id,
                   int64_t          id,
                   const char*      keyword,
                   const char*      match_field,
                   const char*      match_type,
                   int64_t          category_id,
                   const char*      target_type,
                   int              priority,
                   int              is_active)
{
    char uid[32], rid[32], cid[32], prio[32], act[16];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(rid, sizeof(rid), "%lld", (long long)id);
    snprintf(cid, sizeof(cid), "%lld", (long long)category_id);
    snprintf(prio, sizeof(prio), "%d", priority);
    snprintf(act, sizeof(act), "%d", is_active ? 1 : 0);

    const char* cid_param = (category_id > 0) ? cid : NULL;

    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "UPDATE import_rules SET keyword = ?, match_field = ?, match_type = ?, "
        "category_id = ?, target_type = ?, priority = ?, is_active = ? "
        "WHERE user_id = ? AND id = ?",
        (const char*[]){keyword,
                        match_field ? match_field : "all",
                        match_type ? match_type : "contains",
                        cid_param,
                        target_type ? target_type : "expense",
                        prio,
                        act,
                        uid,
                        rid,
                        NULL});

    int ok = res ? csilk_json_array_size(res) > 0 : 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}

int
import_rule_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id)
{
    char uid[32], rid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(rid, sizeof(rid), "%lld", (long long)id);

    csilk_json_t* res =
        csilk_db_query_param_json(pool,
                                  "DELETE FROM import_rules WHERE user_id = ? AND id = ?",
                                  (const char*[]){uid, rid, NULL});

    int ok = res ? csilk_json_array_size(res) > 0 : 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}

static int64_t
find_category_id_by_name(csilk_db_pool_t* pool, int64_t user_id, const char* name)
{
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "SELECT id FROM categories WHERE user_id = ? AND name = ? LIMIT 1",
        (const char*[]){uid, name, NULL});
    int64_t cid = 0;
    if (res && csilk_json_array_size(res) > 0) {
        cid = db_get_int(csilk_json_array_get(res, 0), "id");
    }
    if (res) {
        csilk_json_free(res);
    }
    return cid;
}

void
import_rule_seed_defaults(csilk_db_pool_t* pool, int64_t user_id)
{
    struct DefaultRule {
        const char* keyword;
        const char* category_name;
        const char* target_type;
        int         priority;
    } defaults[] = {
        /* 餐饮美食 */
        {"美团",     "餐饮美食", "expense", 10},
        {"饿了么",   "餐饮美食", "expense", 10},
        {"麦当劳",   "餐饮美食", "expense", 10},
        {"肯德基",   "餐饮美食", "expense", 10},
        {"星巴克",   "餐饮美食", "expense", 10},
        {"瑞幸",     "餐饮美食", "expense", 10},
        {"喜茶",     "餐饮美食", "expense", 10},
        {"奈雪",     "餐饮美食", "expense", 10},
        {"海底捞",   "餐饮美食", "expense", 10},
        {"餐厅",     "餐饮美食", "expense", 20},
        {"咖啡",     "餐饮美食", "expense", 20},
        {"快餐",     "餐饮美食", "expense", 20},

        /* 交通出行 */
        {"滴滴",     "交通出行", "expense", 10},
        {"高德打车", "交通出行", "expense", 10},
        {"花小猪",   "交通出行", "expense", 10},
        {"加油",     "交通出行", "expense", 10},
        {"中石化",   "交通出行", "expense", 10},
        {"中石油",   "交通出行", "expense", 10},
        {"ETC",      "交通出行", "expense", 10},
        {"停车",     "交通出行", "expense", 10},
        {"地铁",     "交通出行", "expense", 10},
        {"公交",     "交通出行", "expense", 10},
        {"中国铁路", "交通出行", "expense", 10},
        {"12306",    "交通出行", "expense", 10},

        /* 日用百货 / 购物消费 */
        {"盒马",     "日用百货", "expense", 10},
        {"山姆",     "日用百货", "expense", 10},
        {"永辉",     "日用百货", "expense", 10},
        {"超市",     "日用百货", "expense", 20},
        {"便利店",   "日用百货", "expense", 20},
        {"京东",     "日用百货", "expense", 20},
        {"淘宝",     "日用百货", "expense", 20},
        {"天猫",     "日用百货", "expense", 20},
        {"拼多多",   "日用百货", "expense", 20},

        /* 居住生活 */
        {"电费",     "居住物业", "expense", 10},
        {"水费",     "居住物业", "expense", 10},
        {"燃气",     "居住物业", "expense", 10},
        {"物业",     "居住物业", "expense", 10},
        {"房租",     "居住物业", "expense", 10},
        {"话费",     "通讯网络", "expense", 10},
        {"中国移动", "通讯网络", "expense", 10},
        {"中国联通", "通讯网络", "expense", 10},
        {"中国电信", "通讯网络", "expense", 10},

        /* 医疗保健 */
        {"医院",     "医疗保健", "expense", 10},
        {"药房",     "医疗保健", "expense", 10},
        {"大药房",   "医疗保健", "expense", 10},
        {"挂号",     "医疗保健", "expense", 10},
        {"诊所",     "医疗保健", "expense", 10},

        /* 收入类型 */
        {"工资",     "工资薪酬", "income",  10},
        {"代发工资", "工资薪酬", "income",  10},
        {"奖金",     "工资薪酬", "income",  10},
        {"年终奖",   "工资薪酬", "income",  10},
        {"报销",     "其他收入", "income",  10},
        {"分红",     "理财收益", "income",  10},
        {"结息",     "理财收益", "income",  10},
    };

    size_t count = sizeof(defaults) / sizeof(defaults[0]);
    for (size_t i = 0; i < count; i++) {
        int64_t cid = find_category_id_by_name(pool, user_id, defaults[i].category_name);
        import_rule_insert(pool,
                           user_id,
                           defaults[i].keyword,
                           "all",
                           "contains",
                           cid,
                           defaults[i].target_type,
                           defaults[i].priority,
                           1);
    }
}
