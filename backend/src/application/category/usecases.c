#include "application/category/usecases.h"
#include "domain/category/rules.h"
#include "infrastructure/repositories/category_repo_impl.h"
#include "common/db.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static pthread_mutex_t s_category_seed_mutex = PTHREAD_MUTEX_INITIALIZER;

#define CATEGORY_SEED_CACHE_CAP 2048
typedef struct {
    const void* pool;
    int64_t     ledger_id;
} category_seed_cache_entry_t;

static category_seed_cache_entry_t s_cat_seed_cache[CATEGORY_SEED_CACHE_CAP];
static size_t                      s_cat_seed_cache_count = 0;
static size_t                      s_cat_seed_cache_next = 0;

static int
cat_seed_cache_contains(const void* pool, int64_t ledger_id)
{
    if (!pool || ledger_id <= 0) {
        return 0;
    }
    for (size_t i = 0; i < s_cat_seed_cache_count; i++) {
        if (s_cat_seed_cache[i].pool == pool && s_cat_seed_cache[i].ledger_id == ledger_id) {
            return 1;
        }
    }
    return 0;
}

static void
cat_seed_cache_add(const void* pool, int64_t ledger_id)
{
    if (!pool || ledger_id <= 0 || cat_seed_cache_contains(pool, ledger_id)) {
        return;
    }
    size_t slot = s_cat_seed_cache_count < CATEGORY_SEED_CACHE_CAP
                      ? s_cat_seed_cache_count++
                      : (s_cat_seed_cache_next++ % CATEGORY_SEED_CACHE_CAP);
    s_cat_seed_cache[slot].pool = pool;
    s_cat_seed_cache[slot].ledger_id = ledger_id;
}

/**
 * @brief 默认分类播种数据 —— 与原 category_service.c 完全一致
 *
 * 为简化迁移，播种逻辑直接复用 infrastructure 层的 find_or_create。
 */

typedef struct {
    const char* parent_name;
    const char* asset_type;
    int         sort_order;
    const char* icon;
    const char* children[10];
    const char* child_icons[10];
} seed_parent_t;

static void
seed_type(void*          pool,
          int64_t        user_id,
          int64_t        ledger_id,
          const char*    type,
          seed_parent_t* items,
          size_t         count,
          const char**   asset_overrides)
{
    for (size_t i = 0; i < count; i++) {
        int64_t pid = mf_category_repo_find_or_create(pool,
                                                      user_id,
                                                      ledger_id,
                                                      items[i].parent_name,
                                                      0,
                                                      type,
                                                      items[i].asset_type,
                                                      items[i].icon,
                                                      items[i].sort_order);
        if (pid > 0) {
            mf_category_child_seed_t batch[10];
            size_t                   child_count = 0;
            for (int j = 0; items[i].children[j] && j < 10; j++) {
                const char* child_at = items[i].asset_type;
                /* Check for per-child asset_type overrides */
                if (asset_overrides) {
                    for (size_t k = 0; asset_overrides[k]; k += 2) {
                        if (strcmp(items[i].children[j], asset_overrides[k]) == 0) {
                            child_at = asset_overrides[k + 1];
                            break;
                        }
                    }
                }
                batch[child_count].name = items[i].children[j];
                batch[child_count].asset_type = child_at;
                batch[child_count].icon = items[i].child_icons[j];
                batch[child_count].sort_order = j + 1;
                child_count++;
            }
            if (child_count > 0) {
                mf_category_repo_insert_children_batch(
                    pool, user_id, ledger_id, pid, type, batch, child_count);
            }
        }
    }
}

void
category_usecase_seed_defaults(void* pool, int64_t user_id, int64_t ledger_id)
{
    pthread_mutex_lock(&s_category_seed_mutex);
    if (ledger_id > 0 && cat_seed_cache_contains(pool, ledger_id)) {
        pthread_mutex_unlock(&s_category_seed_mutex);
        return;
    }
    if (mf_category_repo_is_seeded(pool, user_id, ledger_id)) {
        if (ledger_id > 0) {
            cat_seed_cache_add(pool, ledger_id);
        }
        pthread_mutex_unlock(&s_category_seed_mutex);
        return;
    }

    db_tx_scope_t tx = {0};
    int           tx_ok = (db_tx_scope_begin((csilk_db_pool_t*)pool, "mf_cat_seed", &tx) == 0);

    /* Seed expense categories */
    seed_parent_t expense_items[] = {
        {"餐饮美食",
         "cash",             1,
         "🍜",                         {"早晚餐/正餐", "水果零食", "外卖聚餐", "咖啡奶茶", NULL},
         {"🍜", "🍎", "🥡", "☕", NULL}                                                                                           },
        {"交通出行",
         "cash",             2,
         "🚌",                         {"公共交通", "打车网约车", "加油停车", "飞机高铁", "高速/停车费", NULL},
         {"🚇", "🚕", "⛽", "✈️", "🛣", NULL}                                                                                       },
        {"日常购物",
         "cash",             3,
         "🛍",                          {"服饰鞋包", "日用百货", "数码家电", "生鲜果蔬", "家居清洁", NULL},
         {"👗", "🧴", "💻", "🥬", "🧹", NULL}                                                                                     },
        {"居住缴费",
         "cash",             4,
         "🏠",                         {"房租房贷", "水电燃气", "网络话费", "物业费", "维修家政", NULL},
         {"🏘", "⚡", "📶", "🏢", "🔧", NULL}                                                                                     },
        {"休闲娱乐",
         "cash",             5,
         "🎮",                         {"游戏影视", "运动健身", "旅游度假", "会员订阅", "文娱演出", NULL},
         {"🎬", "🏃", "🏖", "📺", "🎭", NULL}                                                                                     },
        {"医疗健康",
         "cash",             6,
         "🏥",                         {"药品诊疗", "保健体检", "住院手术", "医疗保险", NULL},
         {"💊", "🩺", "🏥", "🛡", NULL}                                                                                            },
        {"人情往来",
         "cash",             7,
         "🎁",                         {"礼金红包", "孝敬父母", "请客送礼", "捐赠公益", NULL},
         {"🧧", "👨", "🎁", "❤️", NULL}                                                                                            },
        {"教育学习",
         "cash",             8,
         "📚",                         {"学费培训", "书籍资料", "在线课程", "考证报名", NULL},
         {"📘", "📖", "💡", "📝", NULL}                                                                                           },
        {"宠物养护",
         "cash",             9,
         "🐾",                         {"宠物食品", "宠物医疗", "宠物用品", "宠物美容", NULL},
         {"🦴", "🐾", "🧸", "✂️", NULL}                                                                                            },
        {"其他支出", "cash", 10, "📋", {"保险费用", "其他杂费", NULL},                                          {"🔒", "📦", NULL}},
    };
    seed_type(pool,
              user_id,
              ledger_id,
              "expense",
              expense_items,
              sizeof(expense_items) / sizeof(expense_items[0]),
              NULL);

    /* Seed income categories */
    seed_parent_t income_items[] = {
        {"职业收入",
         "cash", 1,
         "💼", {"基本工资", "绩效奖金", "兼职外包", "年终奖", "加班费", "补贴津贴", NULL},
         {"💼", "⭐", "🔨", "🎄", "⏰", "💵", NULL}},
        {"投资理财",
         "cash", 2,
         "💹", {"股票/基金收益", "存款利息", "股息分红", "租金收入", "外汇收益", NULL},
         {"📉", "🪙", "📈", "🏠", "💱", NULL}      },
        {"其他收入",
         "cash", 3,
         "📬", {"二手转让", "礼金红包", "政府补贴", "退款返现", "奖学金/补助", NULL},
         {"♻️", "🧧", "📢", "🏷", "🎓", NULL}       },
    };
    seed_type(pool,
              user_id,
              ledger_id,
              "income",
              income_items,
              sizeof(income_items) / sizeof(income_items[0]),
              NULL);

    /* Seed transaction categories */
    seed_parent_t tx_items[] = {
        {"证券交易",
         "cash", 1,
         "📈", {"股票买卖", "基金申赎", "债券买卖", "港股/美股交易", "新股申购", NULL},
         {"📈", "📊", "💎", "🌏", "📋", NULL}},
        {"加密资产",
         "cash", 2,
         "🪙", {"现货买卖", "合约质押", "交易所出入金", NULL},
         {"↕️", "⛓", "🔄", NULL}              },
        {"资金调拨",
         "cash", 3,
         "🔄", {"银证/出入金", "存现/取现", "交易手续费", "资产转移", NULL},
         {"💸", "➕", "🧾", "🔀", NULL}      },
        {"实物投资",
         "cash", 4,
         "🏛", {"贵金属", "收藏品", "黄金积存", NULL},
         {"🥇", "🏛", "🪙", NULL}            },
    };
    seed_type(pool,
              user_id,
              ledger_id,
              "transaction",
              tx_items,
              sizeof(tx_items) / sizeof(tx_items[0]),
              NULL);

    /* Seed asset categories with per-child overrides */
    seed_parent_t asset_items[] = {
        {"流动资产",
         "cash",                    1,
         "💵",                               {"现金账户", "银行存款", "支付宝", "微信零钱", "余额宝/零钱通", "京东金融", NULL},
         {"💰", "🏦", "📱", "💬", "🐷", "🛒", NULL}                                                                                               },
        {"投资资产",
         "stock",                   2,
         "💎",                               {"股票证券", "基金理财", "加密货币", "债券投资", "港美股账户", NULL},
         {"📉", "📊", "⛓", "💎", "🌏", NULL}                                                                                                      },
        {"固定资产", "other_asset", 3, "🏠", {"房产", "车辆", NULL},                                                            {"🏠", "🚗", NULL}},
        {"负债账户",
         "credit_card",             4,
         "💳",                               {"信用卡", "房贷/车贷/贷款", "花呗/白条", "消费贷/网贷", NULL},
         {"💳", "💸", "📲", "📱", NULL}                                                                                                           },
        {"其他资产",
         "other_asset",             5,
         "📦",                               {"应收款项", "预付卡/储值卡", NULL},
         {"🪪", "🎫", NULL}                                                                                                                       },
    };
    const char* asset_overrides[] = {
        "股票证券",    "stock", "基金理财",      "fund", "加密货币", "crypto",         "债券投资",
        "bond",        "房产",  "real_estate",   "车辆", "vehicle",  "房贷/车贷/贷款", "loan",
        "消费贷/网贷", "loan",  "预付卡/储值卡", "cash", "信用卡",   "credit_card",    NULL,
    };
    seed_type(pool,
              user_id,
              ledger_id,
              "asset",
              asset_items,
              sizeof(asset_items) / sizeof(asset_items[0]),
              asset_overrides);

    mf_category_repo_mark_seeded(pool, user_id, ledger_id);
    if (tx_ok) {
        db_tx_scope_commit((csilk_db_pool_t*)pool, &tx);
    }
    if (ledger_id > 0) {
        cat_seed_cache_add(pool, ledger_id);
    }
    pthread_mutex_unlock(&s_category_seed_mutex);
}

int
category_usecase_list_entities(void*           pool,
                               int64_t         user_id,
                               int64_t         ledger_id,
                               const char*     type,
                               mf_category_t** out_list,
                               size_t*         out_count)
{
    return mf_category_repo_list(pool, user_id, ledger_id, type, out_list, out_count);
}

void
category_usecase_free_list(mf_category_t* list, size_t count)
{
    mf_category_repo_free_list(list, count);
}

int
category_usecase_list(
    void* pool, int64_t user_id, int64_t ledger_id, const char* type, csilk_json_t** out_list)
{
    mf_category_t* cats = NULL;
    size_t         count = 0;
    if (mf_category_repo_list(pool, user_id, ledger_id, type, &cats, &count) != 0) {
        return -1;
    }
    csilk_json_t* arr = csilk_json_array();
    for (size_t i = 0; i < count; i++) {
        csilk_json_t* obj = csilk_json_object();
        csilk_json_add_number(obj, "id", (double)cats[i].id);
        csilk_json_add_string(obj, "name", cats[i].name);
        if (cats[i].parent_id > 0) {
            csilk_json_add_number(obj, "parent_id", (double)cats[i].parent_id);
        } else {
            csilk_json_add_null(obj, "parent_id");
        }
        csilk_json_add_string(obj, "type", cats[i].type);
        if (cats[i].asset_type[0]) {
            csilk_json_add_string(obj, "asset_type", cats[i].asset_type);
        }
        if (cats[i].currency[0]) {
            csilk_json_add_string(obj, "currency", cats[i].currency);
        }
        csilk_json_add_string(obj, "icon", cats[i].icon);
        csilk_json_add_number(obj, "sort_order", (double)cats[i].sort_order);
        csilk_json_add_item(arr, obj);
    }
    mf_category_repo_free_list(cats, count);
    *out_list = arr;
    return 0;
}

int
category_usecase_children(
    void* pool, int64_t user_id, int64_t ledger_id, int64_t parent_id, csilk_json_t** out_list)
{
    mf_category_t* cats = NULL;
    size_t         count = 0;
    if (mf_category_repo_children(pool, user_id, ledger_id, parent_id, &cats, &count) != 0) {
        return -1;
    }
    csilk_json_t* arr = csilk_json_array();
    for (size_t i = 0; i < count; i++) {
        csilk_json_t* obj = csilk_json_object();
        csilk_json_add_number(obj, "id", (double)cats[i].id);
        csilk_json_add_string(obj, "name", cats[i].name);
        if (cats[i].parent_id > 0) {
            csilk_json_add_number(obj, "parent_id", (double)cats[i].parent_id);
        } else {
            csilk_json_add_null(obj, "parent_id");
        }
        csilk_json_add_string(obj, "type", cats[i].type);
        if (cats[i].asset_type[0]) {
            csilk_json_add_string(obj, "asset_type", cats[i].asset_type);
        }
        if (cats[i].currency[0]) {
            csilk_json_add_string(obj, "currency", cats[i].currency);
        }
        csilk_json_add_string(obj, "icon", cats[i].icon);
        csilk_json_add_number(obj, "sort_order", (double)cats[i].sort_order);
        csilk_json_add_item(arr, obj);
    }
    mf_category_repo_free_list(cats, count);
    *out_list = arr;
    return 0;
}

int
category_usecase_create(void*                        pool,
                        const create_category_cmd_t* cmd,
                        int64_t*                     out_id,
                        category_usecase_result_t*   out_res)
{
    if (!mf_category_rule_validate_name(cmd->name)) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "分类名称不能为空");
        return -1;
    }
    const char* t = cmd->type && cmd->type[0] ? cmd->type : "asset";
    if (!mf_category_rule_validate_type(t)) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "分类类型无效");
        return -1;
    }
    if (mf_category_repo_exists_sibling(
            pool, cmd->user_id, cmd->ledger_id, cmd->parent_id, t, cmd->name, 0) > 0) {
        out_res->code = 1004;
        snprintf(out_res->message, sizeof(out_res->message), "同级下已存在同名分类");
        return -1;
    }

    mf_category_t cat = {0};
    cat.user_id = cmd->user_id;
    strncpy(cat.name, cmd->name, sizeof(cat.name) - 1);
    cat.parent_id = cmd->parent_id;
    strncpy(cat.type, t, sizeof(cat.type) - 1);
    strncpy(cat.asset_type,
            cmd->asset_type && cmd->asset_type[0] ? cmd->asset_type : "cash",
            sizeof(cat.asset_type) - 1);
    strncpy(cat.currency,
            cmd->currency && cmd->currency[0] ? cmd->currency : "CNY",
            sizeof(cat.currency) - 1);
    strncpy(cat.icon, cmd->icon && cmd->icon[0] ? cmd->icon : "", sizeof(cat.icon) - 1);
    cat.sort_order = cmd->sort_order;

    return mf_category_repo_create(pool, cmd->user_id, cmd->ledger_id, &cat, out_id);
}

int
category_usecase_update(void*                        pool,
                        const update_category_cmd_t* cmd,
                        category_usecase_result_t*   out_res)
{
    int64_t parent_id = 0;
    if (mf_category_repo_get_parent_id(
            pool, cmd->user_id, cmd->ledger_id, cmd->category_id, &parent_id) != 0) {
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "分类不存在");
        return 0;
    }

    const char* t = cmd->type && cmd->type[0] ? cmd->type : "asset";
    if (cmd->name && cmd->name[0] &&
        mf_category_repo_exists_sibling(
            pool, cmd->user_id, cmd->ledger_id, parent_id, t, cmd->name, cmd->category_id) > 0) {
        out_res->code = 1004;
        snprintf(out_res->message, sizeof(out_res->message), "同级下已存在同名分类");
        return -1;
    }

    mf_category_t cat = {0};
    cat.user_id = cmd->user_id;
    if (cmd->name) {
        strncpy(cat.name, cmd->name, sizeof(cat.name) - 1);
    }
    strncpy(cat.type, t, sizeof(cat.type) - 1);
    strncpy(cat.asset_type,
            cmd->asset_type && cmd->asset_type[0] ? cmd->asset_type : "cash",
            sizeof(cat.asset_type) - 1);
    strncpy(cat.currency,
            cmd->currency && cmd->currency[0] ? cmd->currency : "CNY",
            sizeof(cat.currency) - 1);
    strncpy(cat.icon, cmd->icon && cmd->icon[0] ? cmd->icon : "", sizeof(cat.icon) - 1);
    cat.sort_order = cmd->sort_order;

    int rc = mf_category_repo_update(pool, cmd->user_id, cmd->ledger_id, cmd->category_id, &cat);
    if (rc == 1) {
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "分类不存在");
    }
    return rc == 1 ? 0 : rc;
}

int
category_usecase_delete(void*                        pool,
                        const delete_category_cmd_t* cmd,
                        category_usecase_result_t*   out_res)
{
    int64_t child_count = 0;
    if (mf_category_repo_count_children(
            pool, cmd->user_id, cmd->ledger_id, cmd->category_id, &child_count) != 0) {
        return -1;
    }
    if (!mf_category_rule_can_delete(child_count)) {
        out_res->code = 1004;
        snprintf(out_res->message, sizeof(out_res->message), "分类下有子分类，无法删除");
        return -1;
    }
    int rc = mf_category_repo_delete(pool, cmd->user_id, cmd->ledger_id, cmd->category_id);
    if (rc == 1) {
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "分类不存在");
    }
    return rc == 1 ? 0 : rc;
}
