#include "services/category_service.h"
#include "common/db.h"
#include "csilk/csilk.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char* parent_name;
    const char* asset_type;
    int         sort_order;
    const char* icon;
    const char* children[10];
    const char* child_icons[10];
} default_parent_cat_t;

static int64_t
find_or_create_cat(csilk_db_pool_t* pool,
                   int64_t          user_id,
                   const char*      name,
                   int64_t          parent_id,
                   const char*      type,
                   const char*      asset_type,
                   const char*      icon,
                   int              sort_order)
{
    char uid_str[32], pid_str[32], sort_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(pid_str, sizeof(pid_str), "%lld", (long long)parent_id);
    snprintf(sort_str, sizeof(sort_str), "%d", sort_order);

    int64_t id = 0;
    if (parent_id > 0) {
        const char*   sel_params[] = {uid_str, name, pid_str, NULL};
        csilk_json_t* sel = csilk_db_query_param_json(
            pool,
            "SELECT id FROM categories WHERE user_id = ? AND name = ? AND parent_id = ?",
            sel_params);
        if (sel) {
            if (csilk_json_array_size(sel) > 0) {
                id = db_get_int(csilk_json_array_get(sel, 0), "id");
            }
            csilk_json_free(sel);
        }
        if (id > 0) {
            return id;
        }
    } else {
        const char*   sel_params[] = {uid_str, name, NULL};
        csilk_json_t* sel = csilk_db_query_param_json(
            pool,
            "SELECT id FROM categories WHERE user_id = ? AND name = ? AND parent_id IS NULL",
            sel_params);
        if (sel) {
            if (csilk_json_array_size(sel) > 0) {
                id = db_get_int(csilk_json_array_get(sel, 0), "id");
            }
            csilk_json_free(sel);
        }
        if (id > 0) {
            return id;
        }
    }

    csilk_json_t* res = NULL;
    if (parent_id > 0) {
        const char* params[] = {
            uid_str, name, pid_str, type, asset_type, "CNY", icon, sort_str, NULL};
        res = csilk_db_query_param_json(pool,
                                        "INSERT INTO categories (user_id, name, parent_id, type, "
                                        "asset_type, currency, icon, sort_order) "
                                        "VALUES (?, ?, ?, ?, ?, ?, ?, ?) RETURNING id",
                                        params);
    } else {
        const char* params[] = {uid_str, name, type, asset_type, "CNY", icon, sort_str, NULL};
        res = csilk_db_query_param_json(
            pool,
            "INSERT INTO categories (user_id, name, type, asset_type, currency, icon, sort_order) "
            "VALUES (?, ?, ?, ?, ?, ?, ?) RETURNING id",
            params);
    }
    if (res) {
        if (csilk_json_array_size(res) > 0) {
            id = db_get_int(csilk_json_array_get(res, 0), "id");
        }
        csilk_json_free(res);
    }
    return id;
}

static void
ensure_default_categories_for_type(csilk_db_pool_t* pool, int64_t user_id, const char* type)
{
    if (strcmp(type, "expense") == 0) {
        default_parent_cat_t items[] = {
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
            {"其他支出", "cash", 10, "📋", {"保险费用", "其他杂费", NULL},                                          {"🔒", "📦", NULL}}
        };
        for (size_t i = 0; i < sizeof(items) / sizeof(items[0]); i++) {
            int64_t pid = find_or_create_cat(pool,
                                             user_id,
                                             items[i].parent_name,
                                             0,
                                             "expense",
                                             items[i].asset_type,
                                             items[i].icon,
                                             items[i].sort_order);
            if (pid > 0) {
                for (int j = 0; items[i].children[j] != NULL; j++) {
                    find_or_create_cat(pool,
                                       user_id,
                                       items[i].children[j],
                                       pid,
                                       "expense",
                                       items[i].asset_type,
                                       items[i].child_icons[j],
                                       j + 1);
                }
            }
        }
    } else if (strcmp(type, "income") == 0) {
        default_parent_cat_t items[] = {
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
             {"♻️", "🧧", "📢", "🏷", "🎓", NULL}       }
        };
        for (size_t i = 0; i < sizeof(items) / sizeof(items[0]); i++) {
            int64_t pid = find_or_create_cat(pool,
                                             user_id,
                                             items[i].parent_name,
                                             0,
                                             "income",
                                             items[i].asset_type,
                                             items[i].icon,
                                             items[i].sort_order);
            if (pid > 0) {
                for (int j = 0; items[i].children[j] != NULL; j++) {
                    find_or_create_cat(pool,
                                       user_id,
                                       items[i].children[j],
                                       pid,
                                       "income",
                                       items[i].asset_type,
                                       items[i].child_icons[j],
                                       j + 1);
                }
            }
        }
    } else if (strcmp(type, "transaction") == 0) {
        default_parent_cat_t items[] = {
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
             {"🥇", "🏛", "🪙", NULL}            }
        };
        for (size_t i = 0; i < sizeof(items) / sizeof(items[0]); i++) {
            int64_t pid = find_or_create_cat(pool,
                                             user_id,
                                             items[i].parent_name,
                                             0,
                                             "transaction",
                                             items[i].asset_type,
                                             items[i].icon,
                                             items[i].sort_order);
            if (pid > 0) {
                for (int j = 0; items[i].children[j] != NULL; j++) {
                    find_or_create_cat(pool,
                                       user_id,
                                       items[i].children[j],
                                       pid,
                                       "transaction",
                                       items[i].asset_type,
                                       items[i].child_icons[j],
                                       j + 1);
                }
            }
        }
    } else if (strcmp(type, "asset") == 0) {
        default_parent_cat_t items[] = {
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
             {"🪪", "🎫", NULL}                                                                                                                       }
        };
        for (size_t i = 0; i < sizeof(items) / sizeof(items[0]); i++) {
            int64_t pid = find_or_create_cat(pool,
                                             user_id,
                                             items[i].parent_name,
                                             0,
                                             "asset",
                                             items[i].asset_type,
                                             items[i].icon,
                                             items[i].sort_order);
            if (pid > 0) {
                for (int j = 0; items[i].children[j] != NULL; j++) {
                    const char* child_asset_type = items[i].asset_type;
                    if (strcmp(items[i].children[j], "股票证券") == 0) {
                        child_asset_type = "stock";
                    } else if (strcmp(items[i].children[j], "基金理财") == 0) {
                        child_asset_type = "fund";
                    } else if (strcmp(items[i].children[j], "加密货币") == 0) {
                        child_asset_type = "crypto";
                    } else if (strcmp(items[i].children[j], "债券投资") == 0) {
                        child_asset_type = "bond";
                    } else if (strcmp(items[i].children[j], "房产") == 0) {
                        child_asset_type = "real_estate";
                    } else if (strcmp(items[i].children[j], "车辆") == 0) {
                        child_asset_type = "vehicle";
                    } else if (strcmp(items[i].children[j], "房贷/车贷/贷款") == 0) {
                        child_asset_type = "loan";
                    } else if (strcmp(items[i].children[j], "消费贷/网贷") == 0) {
                        child_asset_type = "loan";
                    } else if (strcmp(items[i].children[j], "预付卡/储值卡") == 0) {
                        child_asset_type = "cash";
                    } else if (strcmp(items[i].children[j], "信用卡") == 0) {
                        child_asset_type = "credit_card";
                    }
                    find_or_create_cat(pool,
                                       user_id,
                                       items[i].children[j],
                                       pid,
                                       "asset",
                                       child_asset_type,
                                       items[i].child_icons[j],
                                       j + 1);
                }
            }
        }
    }
}

static void
migrate_legacy_category_names(csilk_db_pool_t* pool, int64_t user_id)
{
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* renames[][2] = {
        {"工资",      "职业收入"},
        {"理财收益",  "投资理财"},
        {"餐饮",      "餐饮美食"},
        {"交通",      "交通出行"},
        {"居住",      "居住缴费"},
        {"购物",      "日常购物"},
        {"娱乐",      "休闲娱乐"},
        {"医疗",      "医疗健康"},
        {"股票/基金", "证券交易"},
        {"加密货币",  "加密资产"},
    };
    for (size_t i = 0; i < sizeof(renames) / sizeof(renames[0]); i++) {
        const char* params[] = {
            renames[i][1], uid_str, renames[i][0], uid_str, renames[i][1], NULL};
        csilk_db_query_param_json(pool,
                                  "UPDATE categories SET name = ? "
                                  "WHERE user_id = ? AND name = ? AND parent_id IS NULL "
                                  "AND NOT EXISTS (SELECT 1 FROM categories t WHERE t.user_id = ? "
                                  "AND t.name = ? AND t.parent_id IS NULL)",
                                  params);
    }
}

void
categories_seed_defaults(csilk_db_pool_t* pool, int64_t user_id)
{
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    // already seeded for this user?
    const char*   st_params[] = {uid_str, NULL};
    csilk_json_t* st = csilk_db_query_param_json(
        pool, "SELECT 1 FROM category_seed_state WHERE user_id = ?", st_params);
    bool seeded = st && csilk_json_array_size(st) > 0;
    if (st) {
        csilk_json_free(st);
    }
    if (seeded) {
        return;
    }

    migrate_legacy_category_names(pool, user_id);
    ensure_default_categories_for_type(pool, user_id, "asset");
    ensure_default_categories_for_type(pool, user_id, "expense");
    ensure_default_categories_for_type(pool, user_id, "income");
    ensure_default_categories_for_type(pool, user_id, "transaction");
    csilk_db_query_param_json(
        pool, "INSERT OR IGNORE INTO category_seed_state (user_id) VALUES (?)", st_params);
}
