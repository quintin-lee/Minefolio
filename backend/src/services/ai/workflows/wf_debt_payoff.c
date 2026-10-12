#include "services/ai/workflows/cashflow_forecast.h"
#include "services/ai/workflows/cashflow_forecast_common.h"
#include "infrastructure/repositories/ai_repo_impl.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

/*  7. Debt Payoff (wf_debt_payoff)                                         */
/* ========================================================================= */

static double
dp_rate_from_note(const char* note, const char* asset_type)
{
    double def = 6.0;
    if (asset_type) {
        if (strcmp(asset_type, "credit_card") == 0) {
            def = 18.0;
        } else if (strcmp(asset_type, "loan") == 0) {
            def = 4.9;
        } else if (strcmp(asset_type, "other_liability") == 0) {
            def = 6.0;
        }
    }
    if (!note || !note[0]) {
        return def;
    }
    const char* pct = strchr(note, '%');
    if (!pct) {
        return def;
    }
    const char* p = pct - 1;
    while (p > note && (*p == ' ' || *p == '\t')) {
        p--;
    }
    const char* num_end = p + 1;
    while (p >= note && ((*p >= '0' && *p <= '9') || *p == '.')) {
        p--;
    }
    const char* num_start = p + 1;
    if (num_start < num_end) {
        char   buf[32] = {0};
        size_t len = (size_t)(num_end - num_start);
        if (len < sizeof(buf)) {
            memcpy(buf, num_start, len);
            buf[len] = '\0';
            double v = atof(buf);
            if (v > 0 && v < 100) {
                return v;
            }
        }
    }
    return def;
}

static char*
step_dp_collect(csilk_db_pool_t*    pool,
                int64_t             user_id,
                const csilk_json_t* params,
                const char*         ctx_json)
{
    (void)params;
    (void)ctx_json;
    int64_t       tot = 0;
    csilk_json_t* list = mf_ai_repo_asset_list(pool, user_id, 1, 200, NULL, &tot);
    csilk_json_t* debts = csilk_json_array();
    double        total_debt = 0;
    double        max_rate = 0;
    double        min_bal = 1e18;
    if (list) {
        size_t n = csilk_json_array_size(list);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* a = csilk_json_array_get(list, i);
            const char*   t = csilk_json_get_string(a, "asset_type")
                                  ? csilk_json_get_string(a, "asset_type")
                                  : "";
            if (strcmp(t, "loan") != 0 && strcmp(t, "credit_card") != 0 &&
                strcmp(t, "other_liability") != 0) {
                continue;
            }
            double bal = db_get_num(a, "current_value");
            if (bal == 0.0) {
                bal = db_get_num(a, "balance");
            }
            if (bal <= 0.0) {
                continue;
            }
            const char* note =
                csilk_json_get_string(a, "note") ? csilk_json_get_string(a, "note") : "";
            double      rate = dp_rate_from_note(note, t);
            int64_t     aid = db_get_int(a, "id");
            const char* nm =
                csilk_json_get_string(a, "name") ? csilk_json_get_string(a, "name") : "未命名负债";
            csilk_json_t* o = csilk_json_object();
            csilk_json_add_number(o, "id", (double)aid);
            csilk_json_add_string(o, "name", nm);
            csilk_json_add_string(o, "asset_type", t);
            csilk_json_add_number(o, "balance", bal);
            csilk_json_add_number(o, "annual_rate", rate);
            csilk_json_add_number(o, "monthly_rate", rate / 12.0 / 100.0);
            csilk_json_add_string(o, "note", note);
            csilk_json_array_append(debts, o);
            total_debt += bal;
            if (rate > max_rate) {
                max_rate = rate;
            }
            if (bal < min_bal) {
                min_bal = bal;
            }
        }
        csilk_json_free(list);
    }
    if (min_bal > 1e17) {
        min_bal = 0;
    }
    csilk_json_t* out = csilk_json_object();
    csilk_json_add_number(out, "debt_count", (double)csilk_json_array_size(debts));
    csilk_json_add_number(out, "total_debt", total_debt);
    csilk_json_add_number(out, "max_rate", max_rate);
    csilk_json_add_number(out, "min_balance", min_bal);
    csilk_json_add_array(out, "debts", debts);
    size_t len = 0;
    char*  s = csilk_json_serialize(out, &len);
    csilk_json_free(out);
    char* ret = s ? strdup(s) : strdup("{}");
    free(s);
    return ret;
}

typedef struct {
    double balance;
    double annual_rate;
    double monthly_rate;
    char   name[128];
    char   type[32];
} dp_item_t;

static int
cmp_avalanche(const void* a, const void* b)
{
    const dp_item_t* x = (const dp_item_t*)a;
    const dp_item_t* y = (const dp_item_t*)b;
    if (y->annual_rate > x->annual_rate) {
        return 1;
    }
    if (y->annual_rate < x->annual_rate) {
        return -1;
    }
    return 0;
}

static int
cmp_snowball(const void* a, const void* b)
{
    const dp_item_t* x = (const dp_item_t*)a;
    const dp_item_t* y = (const dp_item_t*)b;
    if (x->balance < y->balance) {
        return -1;
    }
    if (x->balance > y->balance) {
        return 1;
    }
    return 0;
}

static double
dp_simulate(dp_item_t* items, size_t n, double monthly_payment, int months, double* out_interest)
{
    double total_interest = 0;
    for (int m = 0; m < months; m++) {
        for (size_t i = 0; i < n; i++) {
            if (items[i].balance <= 0.01) {
                continue;
            }
            double interest = items[i].balance * items[i].monthly_rate;
            items[i].balance += interest;
            total_interest += interest;
        }
        double remaining = monthly_payment;
        for (size_t i = 0; i < n; i++) {
            if (items[i].balance <= 0.01) {
                continue;
            }
            double min_pay = items[i].balance * 0.02;
            if (min_pay < 100) {
                min_pay = 100;
            }
            if (min_pay > items[i].balance) {
                min_pay = items[i].balance;
            }
            double take = remaining >= min_pay ? min_pay : remaining;
            if (take > items[i].balance) {
                take = items[i].balance;
            }
            items[i].balance -= take;
            remaining -= take;
            if (remaining <= 0.01) {
                break;
            }
        }
        for (size_t i = 0; i < n && remaining > 0.01; i++) {
            if (items[i].balance <= 0.01) {
                continue;
            }
            double take = remaining > items[i].balance ? items[i].balance : remaining;
            items[i].balance -= take;
            remaining -= take;
        }
        int all_zero = 1;
        for (size_t i = 0; i < n; i++) {
            if (items[i].balance > 0.01) {
                all_zero = 0;
                break;
            }
        }
        if (all_zero) {
            break;
        }
    }
    double remaining_debt = 0;
    for (size_t i = 0; i < n; i++) {
        if (items[i].balance > 0.01) {
            remaining_debt += items[i].balance;
        }
    }
    if (out_interest) {
        *out_interest = total_interest;
    }
    return remaining_debt;
}

static char*
step_dp_simulate(csilk_db_pool_t*    pool,
                 int64_t             user_id,
                 const csilk_json_t* params,
                 const char*         ctx_json)
{
    (void)pool;
    (void)user_id;
    csilk_json_t* root = ctx_json ? csilk_json_parse(ctx_json) : NULL;
    csilk_json_t* s0 = root ? csilk_json_get(root, "dp_collect") : NULL;
    csilk_json_t* debts = s0 ? csilk_json_get(s0, "debts") : NULL;
    size_t        n = (debts && csilk_json_is_array(debts)) ? csilk_json_array_size(debts) : 0;

    double monthly_payment = 0;
    if (params) {
        monthly_payment = db_get_num(params, "monthly_payment");
        if (monthly_payment <= 0) {
            monthly_payment = db_get_num(params, "amount");
        }
    }
    if (monthly_payment <= 0) {
        double total_debt = s0 ? db_get_num(s0, "total_debt") : 0;
        monthly_payment = total_debt / 12.0;
        if (monthly_payment < 2000) {
            monthly_payment = 2000;
        }
    }

    dp_item_t* items = NULL;
    if (n > 0) {
        items = (dp_item_t*)calloc(n, sizeof(dp_item_t));
        for (size_t i = 0; i < n; i++) {
            const csilk_json_t* d = csilk_json_array_get(debts, i);
            items[i].balance = db_get_num(d, "balance");
            items[i].annual_rate = db_get_num(d, "annual_rate");
            items[i].monthly_rate = items[i].annual_rate / 12.0 / 100.0;
            const char* nm =
                csilk_json_get_string(d, "name") ? csilk_json_get_string(d, "name") : "";
            const char* tp = csilk_json_get_string(d, "asset_type")
                                 ? csilk_json_get_string(d, "asset_type")
                                 : "";
            strncpy(items[i].name, nm, sizeof(items[i].name) - 1);
            strncpy(items[i].type, tp, sizeof(items[i].type) - 1);
        }
    }

    double interest_av = 0, interest_sb = 0;
    double remain_av = 0, remain_sb = 0;

    if (n > 0 && items) {
        dp_item_t* av = (dp_item_t*)calloc(n, sizeof(dp_item_t));
        dp_item_t* sb = (dp_item_t*)calloc(n, sizeof(dp_item_t));
        memcpy(av, items, n * sizeof(dp_item_t));
        memcpy(sb, items, n * sizeof(dp_item_t));
        qsort(av, n, sizeof(dp_item_t), cmp_avalanche);
        qsort(sb, n, sizeof(dp_item_t), cmp_snowball);
        remain_av = dp_simulate(av, n, monthly_payment, 12, &interest_av);
        remain_sb = dp_simulate(sb, n, monthly_payment, 12, &interest_sb);
        free(av);
        free(sb);
    }

    double      saved = interest_sb - interest_av;
    const char* recommended = (interest_av <= interest_sb) ? "avalanche" : "snowball";

    if (root) {
        csilk_json_free(root);
    }
    if (items) {
        free(items);
    }

    csilk_json_t* out = csilk_json_object();
    csilk_json_add_number(out, "debt_count", (double)n);
    csilk_json_add_number(out, "monthly_payment", monthly_payment);
    csilk_json_add_number(out, "avalanche_interest", interest_av);
    csilk_json_add_number(out, "avalanche_remaining", remain_av);
    csilk_json_add_number(out, "snowball_interest", interest_sb);
    csilk_json_add_number(out, "snowball_remaining", remain_sb);
    csilk_json_add_number(out, "interest_saved", saved > 0 ? saved : 0);
    csilk_json_add_string(out, "recommended", recommended);
    size_t len = 0;
    char*  s = csilk_json_serialize(out, &len);
    csilk_json_free(out);
    char* ret = s ? strdup(s) : strdup("{}");
    free(s);
    return ret;
}

static char*
step_dp_report(csilk_db_pool_t*    pool,
               int64_t             user_id,
               const csilk_json_t* params,
               const char*         ctx_json)
{
    (void)pool;
    (void)user_id;
    (void)params;
    csilk_json_t* root = ctx_json ? csilk_json_parse(ctx_json) : NULL;
    csilk_json_t* s0 = root ? csilk_json_get(root, "dp_collect") : NULL;
    csilk_json_t* s1 = root ? csilk_json_get(root, "dp_simulate") : NULL;
    double        total_debt = s0 ? db_get_num(s0, "total_debt") : 0;
    double        debt_cnt = s0 ? db_get_num(s0, "debt_count") : 0;
    csilk_json_t* debts = s0 ? csilk_json_get(s0, "debts") : NULL;
    size_t        n = (debts && csilk_json_is_array(debts)) ? csilk_json_array_size(debts) : 0;
    double        mp = s1 ? db_get_num(s1, "monthly_payment") : 2000;
    double        av_i = s1 ? db_get_num(s1, "avalanche_interest") : 0;
    double        av_r = s1 ? db_get_num(s1, "avalanche_remaining") : 0;
    double        sb_i = s1 ? db_get_num(s1, "snowball_interest") : 0;
    double        sb_r = s1 ? db_get_num(s1, "snowball_remaining") : 0;
    double        saved = s1 ? db_get_num(s1, "interest_saved") : 0;
    const char*   rec = s1 ? csilk_json_get_string(s1, "recommended") : "avalanche";
    if (!rec) {
        rec = "avalanche";
    }

    char mermaid[4096] = {0};
    if (n == 0) {
        snprintf(mermaid,
                 sizeof(mermaid),
                 "> ✅ **无负债**：当前未检测到贷款/信用卡类负债，无需偿还规划。\n\n");
    } else {
        snprintf(mermaid,
                 sizeof(mermaid),
                 "```mermaid\npie showData\n    title 债务余额构成 共￥%.0f\n",
                 total_debt);
        for (size_t i = 0; i < n && i < 8; i++) {
            const csilk_json_t* d = csilk_json_array_get(debts, i);
            const char*         nm =
                csilk_json_get_string(d, "name") ? csilk_json_get_string(d, "name") : "负债";
            double bal = db_get_num(d, "balance");
            char   safe[64] = {0};
            strncpy(safe, nm, sizeof(safe) - 1);
            for (char* p = safe; *p; p++) {
                if (*p == '"' || *p == '\'' || *p == '|') {
                    *p = ' ';
                }
            }
            char line[160];
            snprintf(line, sizeof(line), "    \"%s\" : %.2f\n", safe, bal);
            strncat(mermaid, line, sizeof(mermaid) - strlen(mermaid) - 1);
        }
        strncat(mermaid, "```\n\n", sizeof(mermaid) - strlen(mermaid) - 1);
        char bar[1024] = {0};
        snprintf(bar,
                 sizeof(bar),
                 "```mermaid\nxychart-beta\n    title \"12期利息对比 (越低越好)\"\n"
                 "    x-axis [\"雪崩\",\"雪球\"]\n"
                 "    y-axis \"利息 ￥\" 0 --> %.0f\n"
                 "    bar [%.0f, %.0f]\n```\n\n",
                 (av_i > sb_i ? av_i : sb_i) * 1.2 + 10,
                 av_i,
                 sb_i);
        strncat(mermaid, bar, sizeof(mermaid) - strlen(mermaid) - 1);
    }

    char rows[8192] = {0};
    for (size_t i = 0; i < n && i < 15; i++) {
        const csilk_json_t* d = csilk_json_array_get(debts, i);
        const char* nm = csilk_json_get_string(d, "name") ? csilk_json_get_string(d, "name") : "-";
        const char* tp =
            csilk_json_get_string(d, "asset_type") ? csilk_json_get_string(d, "asset_type") : "-";
        double      bal = db_get_num(d, "balance");
        double      rate = db_get_num(d, "annual_rate");
        const char* type_label = (strcmp(tp, "credit_card") == 0)
                                     ? "信用卡"
                                     : (strcmp(tp, "loan") == 0 ? "贷款" : "其他负债");
        char        safe[64] = {0};
        strncpy(safe, nm, sizeof(safe) - 1);
        for (char* p = safe; *p; p++) {
            if (*p == '|') {
                *p = '/';
            }
        }
        char line[256];
        snprintf(line,
                 sizeof(line),
                 "| %s | %s | ￥%.0f | %.1f%% | %.0f |\n",
                 safe,
                 type_label,
                 bal,
                 rate,
                 bal * rate / 100.0 / 12.0);
        strncat(rows, line, sizeof(rows) - strlen(rows) - 1);
    }
    if (!rows[0]) {
        snprintf(rows, sizeof(rows), "| — | — | — | — | — |\n");
    }

    const char* rec_label =
        (strcmp(rec, "avalanche") == 0) ? "❄️ 雪崩法（优先高利率）" : "⛄ 雪球法（优先小余额）";
    const char* rec_reason = (strcmp(rec, "avalanche") == 0) ? "利息最省，适合理性省钱优先"
                                                             : "成就感强、易坚持，适合需要正反馈";

    char buf[16384];
    snprintf(buf,
             sizeof(buf),
             "### 💳 债务加速偿还规划报告\n\n"
             "**负债笔数** `%d` ｜ **总余额** `￥%.2f` ｜ **月供假设** `￥%.0f/月` ｜ **推荐策略** "
             "`%s`\n\n"
             "#### 一、债务构成\n"
             "%s"
             "| 名称 | 类型 | 余额 | 年利率 | 月利息估算 |\n"
             "| :--- | :--- | :--- | :--- | :--- |\n"
             "%s\n"
             "#### 二、12 期模拟对比（月供 ￥%.0f）\n"
             "| 策略 | 12期总利息 | 12期后剩余 | 特点 |\n"
             "| :--- | :--- | :--- | :--- |\n"
             "| ❄️ 雪崩法（高利率优先） | ￥%.0f | ￥%.0f | 利息最省 |\n"
             "| ⛄ 雪球法（小余额优先） | ￥%.0f | ￥%.0f | 成就感强 |\n\n"
             "**利息差额**：雪崩法 12 期可比雪球法节省 **￥%.0f**\n\n"
             "#### 三、执行建议\n"
             "1. **推荐**：%s — %s；\n"
             "2. **最低还款**：每笔至少还 `max(100, 余额×2%%)`，剩余集中攻克目标债务；\n"
             "3. **利率陷阱**：信用卡 18%% 年化月息约 1.5%%，务必优先清偿；\n"
             "4. **额外还款**：任意额外收入（奖金/退税）直接追加到目标债务，可显著缩短周期。\n"
             "```action\n"
             "{\n"
             "  \"action_type\": \"debt_payoff\",\n"
             "  \"debt_count\": %d,\n"
             "  \"total_debt\": %.2f,\n"
             "  \"monthly_payment\": %.2f,\n"
             "  \"recommended\": \"%s\",\n"
             "  \"interest_saved\": %.2f\n"
             "}\n"
             "```\n",
             (int)debt_cnt,
             total_debt,
             mp,
             rec_label,
             mermaid,
             rows,
             mp,
             av_i,
             av_r,
             sb_i,
             sb_r,
             saved,
             rec_label,
             rec_reason,
             (int)debt_cnt,
             total_debt,
             mp,
             rec,
             saved);

    if (root) {
        csilk_json_free(root);
    }
    return strdup(buf);
}

static const ai_workflow_graph_t g_dp_graph = {
    .id = "wf_debt_payoff",
    .title = "债务加速偿还规划",
    .description =
        "汇总房贷/车贷/信用卡等多头负债，测算雪崩法（利息最省）与雪球法（阻力最小）最优清偿路径。",
    .icon = "ph:credit-card",
    .node_count = 3,
    .nodes =
        {
                {"dp_collect",
             "负债明细与利率盘点",
             "汇总所有负债账户余额与对应年化利率",
             step_dp_collect},
                {"dp_simulate",
             "雪崩 vs 雪球策略模拟",
             "多期动态模拟两种策略利息总额与偿清周期",
             step_dp_simulate},
                {"generate_report",
             "清偿大屏与最优方案推荐",
             "输出利息对比柱状图与分步还款规划表",
             step_dp_report},
                },
};

const ai_workflow_graph_t*
ai_workflow_debt_payoff_get_graph(void)
{
    return &g_dp_graph;
}
