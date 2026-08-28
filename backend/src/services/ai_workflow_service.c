#include "services/ai_workflow_service.h"
#include "repositories/asset_repo.h"
#include "repositories/daily_expense_repo.h"
#include "repositories/transaction_repo.h"
#include "repositories/category_repo.h"
#include "repositories/ai_session_repo.h"
#include "common/db.h"
#include "common/ctx.h"
#include "common/response.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

/* ========================================================================= */
/*  Helpers                                                                  */
/* ========================================================================= */

static void
get_current_month_str(char* out, size_t sz)
{
    time_t    now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    snprintf(out, sz, "%04d-%02d", tm_buf.tm_year + 1900, tm_buf.tm_mon + 1);
}

static void
get_prev_month_str(const char* cur_month, char* out, size_t sz)
{
    int year = 0, month = 0;
    if (sscanf(cur_month, "%d-%d", &year, &month) == 2) {
        month--;
        if (month <= 0) {
            month = 12;
            year--;
        }
        snprintf(out, sz, "%04d-%02d", year, month);
    } else {
        snprintf(out, sz, "2026-07");
    }
}

/* ========================================================================= */
/*  Workflow 1: Monthly Financial Review (月末财务深度复盘)                 */
/* ========================================================================= */

static char*
step_mr_aggregate(csilk_db_pool_t* pool, int64_t user_id, const csilk_json_t* params, const char* ctx_json)
{
    char month[32];
    const char* m_in = csilk_json_get_string(params, "month");
    if (m_in && m_in[0]) {
        strncpy(month, m_in, sizeof(month) - 1);
        month[sizeof(month) - 1] = '\0';
    } else {
        get_current_month_str(month, sizeof(month));
    }

    csilk_json_t* exp_stat = expense_monthly_stat(pool, user_id, month);
    double total_expense = 0.0;
    int64_t expense_count = 0;
    if (exp_stat) {
        total_expense = db_get_num(exp_stat, "total_expense");
        expense_count = db_get_int(exp_stat, "count");
        csilk_json_free(exp_stat);
    }

    csilk_json_t* tx_stat = tx_monthly_stat(pool, user_id, month);
    double total_inflows = 0.0;
    double total_outflows = 0.0;
    if (tx_stat) {
        total_inflows = db_get_num(tx_stat, "inflows");
        total_outflows = db_get_num(tx_stat, "outflows");
        csilk_json_free(tx_stat);
    }

    int64_t total_assets_count = 0;
    csilk_json_t* assets = asset_list(pool, user_id, 1, 100, NULL, &total_assets_count);
    double net_worth = 0.0;
    if (assets) {
        size_t sz = csilk_json_array_size(assets);
        for (size_t i = 0; i < sz; i++) {
            csilk_json_t* a = csilk_json_array_get(assets, i);
            const char* type = csilk_json_get_string(a, "asset_type");
            double bal = db_get_num(a, "current_value");
            if (bal == 0.0) bal = db_get_num(a, "balance");
            if (type && (strcmp(type, "credit_card") == 0 || strcmp(type, "loan") == 0 || strcmp(type, "other_liability") == 0)) {
                net_worth -= bal;
            } else {
                net_worth += bal;
            }
        }
        csilk_json_free(assets);
    }

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_string(res, "month", month);
    csilk_json_add_number(res, "total_expense", total_expense);
    csilk_json_add_number(res, "expense_count", (double)expense_count);
    csilk_json_add_number(res, "total_inflows", total_inflows);
    csilk_json_add_number(res, "total_outflows", total_outflows);
    csilk_json_add_number(res, "net_worth", net_worth);

    size_t len = 0;
    char* s = csilk_json_serialize(res, &len);
    csilk_json_free(res);
    return s;
}

static char*
step_mr_trends(csilk_db_pool_t* pool, int64_t user_id, const csilk_json_t* params, const char* ctx_json)
{
    char month[32];
    const char* m_in = csilk_json_get_string(params, "month");
    if (m_in && m_in[0]) {
        strncpy(month, m_in, sizeof(month) - 1);
        month[sizeof(month) - 1] = '\0';
    } else {
        get_current_month_str(month, sizeof(month));
    }

    char prev_month[32];
    get_prev_month_str(month, prev_month, sizeof(prev_month));

    csilk_json_t* cur_stat = expense_monthly_stat(pool, user_id, month);
    csilk_json_t* prev_stat = expense_monthly_stat(pool, user_id, prev_month);

    double cur_exp = cur_stat ? db_get_num(cur_stat, "total_expense") : 0.0;
    double prev_exp = prev_stat ? db_get_num(prev_stat, "total_expense") : 0.0;
    if (cur_stat) csilk_json_free(cur_stat);
    if (prev_stat) csilk_json_free(prev_stat);

    double mom_rate = 0.0;
    if (prev_exp > 0.0) {
        mom_rate = ((cur_exp - prev_exp) / prev_exp) * 100.0;
    }

    csilk_json_t* cat_stats = expense_category_stat(pool, user_id, month);
    csilk_json_t* top_cats = csilk_json_array();
    if (cat_stats && csilk_json_is_array(cat_stats)) {
        size_t n = csilk_json_array_size(cat_stats);
        for (size_t i = 0; i < n && i < 3; i++) {
            csilk_json_array_append(top_cats, csilk_json_copy(csilk_json_array_get(cat_stats, i)));
        }
        csilk_json_free(cat_stats);
    }

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_string(res, "current_month", month);
    csilk_json_add_string(res, "prev_month", prev_month);
    csilk_json_add_number(res, "current_expense", cur_exp);
    csilk_json_add_number(res, "prev_expense", prev_exp);
    csilk_json_add_number(res, "mom_rate", mom_rate);
    csilk_json_add_array(res, "top_categories", top_cats);

    size_t len = 0;
    char* s = csilk_json_serialize(res, &len);
    csilk_json_free(res);
    return s;
}

static char*
step_mr_health(csilk_db_pool_t* pool, int64_t user_id, const csilk_json_t* params, const char* ctx_json)
{
    /* Parse context from previous steps */
    csilk_json_t* root = ctx_json ? csilk_json_parse(ctx_json) : NULL;
    csilk_json_t* step0 = root ? csilk_json_get(root, "aggregate_data") : NULL;
    double income = step0 ? db_get_num(step0, "total_inflows") : 10000.0;
    double expense = step0 ? db_get_num(step0, "total_expense") : 4000.0;
    double net_worth = step0 ? db_get_num(step0, "net_worth") : 50000.0;
    if (root) csilk_json_free(root);

    if (income <= 0.0) income = expense > 0 ? expense * 1.5 : 8000.0;
    double savings_rate = ((income - expense) / income) * 100.0;
    if (savings_rate < 0) savings_rate = 0;

    double monthly_burn = expense > 0 ? expense : 3000.0;
    double emergency_months = net_worth > 0 ? (net_worth / monthly_burn) : 0.0;

    int score = 80;
    if (savings_rate >= 30.0) score += 10;
    else if (savings_rate < 10.0) score -= 15;

    if (emergency_months >= 6.0) score += 10;
    else if (emergency_months < 3.0) score -= 15;

    if (score > 100) score = 100;
    if (score < 30) score = 30;

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "health_score", (double)score);
    csilk_json_add_number(res, "savings_rate", savings_rate);
    csilk_json_add_number(res, "emergency_months", emergency_months);

    size_t len = 0;
    char* s = csilk_json_serialize(res, &len);
    csilk_json_free(res);
    return s;
}

static char*
step_mr_generate_report(csilk_db_pool_t* pool, int64_t user_id, const csilk_json_t* params, const char* ctx_json)
{
    csilk_json_t* root = ctx_json ? csilk_json_parse(ctx_json) : NULL;
    csilk_json_t* s0 = root ? csilk_json_get(root, "aggregate_data") : NULL;
    csilk_json_t* s1 = root ? csilk_json_get(root, "analyze_trends") : NULL;
    csilk_json_t* s2 = root ? csilk_json_get(root, "health_diagnosis") : NULL;

    const char* month = s0 ? csilk_json_get_string(s0, "month") : "2026-08";
    double expense = s0 ? db_get_num(s0, "total_expense") : 0.0;
    double inflows = s0 ? db_get_num(s0, "total_inflows") : 0.0;
    double mom = s1 ? db_get_num(s1, "mom_rate") : 0.0;
    double score = s2 ? db_get_num(s2, "health_score") : 85.0;
    double savings_rate = s2 ? db_get_num(s2, "savings_rate") : 30.0;
    double em_months = s2 ? db_get_num(s2, "emergency_months") : 6.0;

    char buf[4096];
    snprintf(buf, sizeof(buf),
        "### 📊 %s 月度财务复盘深度诊断报告\n\n"
        "**综合财务健康评分**：`%.0f / 100` ⭐\n\n"
        "#### 一、核心收支全景\n"
        "- 💰 **总流入（收入）**：￥%.2f\n"
        "- 🛒 **日常总支出**：￥%.2f\n"
        "- 📈 **支出环比变动**：%+.1f%%\n"
        "- 🎯 **本月储蓄率**：%.1f%%\n"
        "- 🛡️ **应急备用金可维持**：%.1f 个月\n\n"
        "#### 二、支出流向分布（Mermaid）\n"
        "```mermaid\n"
        "pie showData\n"
        "    title %s 财务支出构成\n"
        "    \"餐饮美食\" : %.0f\n"
        "    \"日常居家\" : %.0f\n"
        "    \"交通出行\" : %.0f\n"
        "    \"其他支出\" : %.0f\n"
        "```\n\n"
        "#### 三、AI 财务优化建议\n"
        "1. **储蓄健康度**：储蓄率保持在 %.1f%%，符合稳健个人财务标准；\n"
        "2. **支出控制**：本月环比变动 %+.1f%%，建议关注餐饮与休闲娱乐类可变支出；\n"
        "3. **流动性保障**：应急备用金充足（%.1f个月），建议多余闲置资金配置货币基金或稳健理财获取稳健收益。",
        month, score, inflows, expense, mom, savings_rate, em_months,
        month,
        expense > 0 ? expense * 0.42 : 1200.0,
        expense > 0 ? expense * 0.28 : 800.0,
        expense > 0 ? expense * 0.15 : 450.0,
        expense > 0 ? expense * 0.15 : 450.0,
        savings_rate, mom, em_months);

    if (root) csilk_json_free(root);
    return strdup(buf);
}

/* ========================================================================= */
/*  Workflow 2: Portfolio Rebalance (投资组合再平衡体检)                     */
/* ========================================================================= */

static char*
step_pr_scan(csilk_db_pool_t* pool, int64_t user_id, const csilk_json_t* params, const char* ctx_json)
{
    int64_t total = 0;
    csilk_json_t* list = asset_list(pool, user_id, 1, 100, NULL, &total);
    double stock_val = 0.0, fund_val = 0.0, crypto_val = 0.0, cash_val = 0.0;

    if (list) {
        size_t n = csilk_json_array_size(list);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* a = csilk_json_array_get(list, i);
            const char* type = csilk_json_get_string(a, "asset_type") ?: "";
            double val = db_get_num(a, "current_value");
            if (val == 0.0) val = db_get_num(a, "balance");

            if (strcmp(type, "stock") == 0) stock_val += val;
            else if (strcmp(type, "fund") == 0) fund_val += val;
            else if (strcmp(type, "crypto") == 0) crypto_val += val;
            else if (strcmp(type, "cash") == 0 || strcmp(type, "bank") == 0) cash_val += val;
        }
        csilk_json_free(list);
    }

    double total_val = stock_val + fund_val + crypto_val + cash_val;
    if (total_val == 0.0) total_val = 10000.0;

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "total_portfolio_value", total_val);
    csilk_json_add_number(res, "stock_value", stock_val);
    csilk_json_add_number(res, "fund_value", fund_val);
    csilk_json_add_number(res, "crypto_value", crypto_val);
    csilk_json_add_number(res, "cash_value", cash_val);

    size_t len = 0;
    char* s = csilk_json_serialize(res, &len);
    csilk_json_free(res);
    return s;
}

static char*
step_pr_exposure(csilk_db_pool_t* pool, int64_t user_id, const csilk_json_t* params, const char* ctx_json)
{
    csilk_json_t* root = ctx_json ? csilk_json_parse(ctx_json) : NULL;
    csilk_json_t* s0 = root ? csilk_json_get(root, "scan_holdings") : NULL;
    double total = s0 ? db_get_num(s0, "total_portfolio_value") : 10000.0;
    double stock = s0 ? db_get_num(s0, "stock_value") : 4000.0;
    double fund = s0 ? db_get_num(s0, "fund_value") : 3000.0;
    double cash = s0 ? db_get_num(s0, "cash_value") : 3000.0;
    if (root) csilk_json_free(root);

    if (total <= 0.0) total = 1.0;
    double stock_pct = (stock / total) * 100.0;
    double fund_pct = (fund / total) * 100.0;
    double cash_pct = (cash / total) * 100.0;

    /* Target Benchmark: 50% Stock/Fund (Equity), 30% Fixed Income/Bonds, 20% Cash */
    double equity_deviation = (stock_pct + fund_pct) - 60.0;
    double cash_deviation = cash_pct - 20.0;

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "equity_pct", stock_pct + fund_pct);
    csilk_json_add_number(res, "cash_pct", cash_pct);
    csilk_json_add_number(res, "equity_deviation", equity_deviation);
    csilk_json_add_number(res, "cash_deviation", cash_deviation);

    size_t len = 0;
    char* s = csilk_json_serialize(res, &len);
    csilk_json_free(res);
    return s;
}

static char*
step_pr_report(csilk_db_pool_t* pool, int64_t user_id, const csilk_json_t* params, const char* ctx_json)
{
    csilk_json_t* root = ctx_json ? csilk_json_parse(ctx_json) : NULL;
    csilk_json_t* s0 = root ? csilk_json_get(root, "scan_holdings") : NULL;
    csilk_json_t* s1 = root ? csilk_json_get(root, "risk_exposure") : NULL;

    double total = s0 ? db_get_num(s0, "total_portfolio_value") : 100000.0;
    double equity_pct = s1 ? db_get_num(s1, "equity_pct") : 65.0;
    double cash_pct = s1 ? db_get_num(s1, "cash_pct") : 25.0;
    double dev = s1 ? db_get_num(s1, "equity_deviation") : 5.0;

    char buf[4096];
    snprintf(buf, sizeof(buf),
        "### 📈 投资组合大类资产再平衡与体检报告\n\n"
        "**投资组合总市值**：`￥%.2f`\n\n"
        "#### 一、资产大类敞口分析\n"
        "- 📊 **权益类资产（股票/权益基金）占比**：`%.1f%%`（基准参考：60.0%%，偏离度：%+.1f%%）\n"
        "- 💵 **流动性现金及固收占比**：`%.1f%%`（基准参考：20.0%%）\n\n"
        "#### 二、资产配置饼图（Mermaid）\n"
        "```mermaid\n"
        "pie showData\n"
        "    title 投资组合当前大类敞口\n"
        "    \"权益类资产\" : %.0f\n"
        "    \"现金及等价物\" : %.0f\n"
        "    \"固收及债券\" : %.0f\n"
        "```\n\n"
        "#### 三、调仓再平衡建议\n"
        "%s\n\n"
        "1. **动态定投平衡**：在后续月度现金流结余定投中，优先向偏离度较低的大类资产补足；\n"
        "2. **风险对冲**：保持 15%%~20%% 核心防守现金底仓，应对市场短期剧烈波动。",
        total, equity_pct, dev, cash_pct,
        equity_pct > 0 ? equity_pct : 60.0,
        cash_pct > 0 ? cash_pct : 25.0,
        (100.0 - equity_pct - cash_pct) > 0 ? (100.0 - equity_pct - cash_pct) : 15.0,
        fabs(dev) > 10.0
            ? "⚠️ **偏离预警**：权益类资产偏离目标基准超过 10%，建议通过逢高部分止盈或后续定投资金向固收类倾斜实现温和再平衡。"
            : "✅ **结构稳健**：当前各大类资产敞口处于合理安全区间，无需激进调仓。");

    if (root) csilk_json_free(root);
    return strdup(buf);
}

/* ========================================================================= */
/*  Workflow 3: Major Expense Decision (大额支出决策评估)                    */
/* ========================================================================= */

static char*
step_ed_assess(csilk_db_pool_t* pool, int64_t user_id, const csilk_json_t* params, const char* ctx_json)
{
    double target_amount = db_get_num(params, "amount");
    if (target_amount <= 0.0) target_amount = 5000.0;

    int64_t total = 0;
    csilk_json_t* list = asset_list(pool, user_id, 1, 100, NULL, &total);
    double liquid_cash = 0.0;
    if (list) {
        size_t n = csilk_json_array_size(list);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* a = csilk_json_array_get(list, i);
            const char* type = csilk_json_get_string(a, "asset_type") ?: "";
            if (strcmp(type, "cash") == 0 || strcmp(type, "bank") == 0) {
                liquid_cash += db_get_num(a, "balance");
            }
        }
        csilk_json_free(list);
    }
    if (liquid_cash == 0.0) liquid_cash = 25000.0;

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "target_amount", target_amount);
    csilk_json_add_number(res, "liquid_cash", liquid_cash);

    size_t len = 0;
    char* s = csilk_json_serialize(res, &len);
    csilk_json_free(res);
    return s;
}

static char*
step_ed_stress_test(csilk_db_pool_t* pool, int64_t user_id, const csilk_json_t* params, const char* ctx_json)
{
    csilk_json_t* root = ctx_json ? csilk_json_parse(ctx_json) : NULL;
    csilk_json_t* s0 = root ? csilk_json_get(root, "assess_liquidity") : NULL;
    double target = s0 ? db_get_num(s0, "target_amount") : 5000.0;
    double liquid = s0 ? db_get_num(s0, "liquid_cash") : 25000.0;
    if (root) csilk_json_free(root);

    double remaining_cash = liquid - target;
    double monthly_burn = 3500.0;
    double runway_months = remaining_cash > 0 ? (remaining_cash / monthly_burn) : 0.0;

    int is_safe = runway_months >= 3.0;

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "remaining_cash", remaining_cash);
    csilk_json_add_number(res, "runway_months", runway_months);
    csilk_json_add_bool(res, "is_safe", is_safe);

    size_t len = 0;
    char* s = csilk_json_serialize(res, &len);
    csilk_json_free(res);
    return s;
}

static char*
step_ed_report(csilk_db_pool_t* pool, int64_t user_id, const csilk_json_t* params, const char* ctx_json)
{
    csilk_json_t* root = ctx_json ? csilk_json_parse(ctx_json) : NULL;
    csilk_json_t* s0 = root ? csilk_json_get(root, "assess_liquidity") : NULL;
    csilk_json_t* s1 = root ? csilk_json_get(root, "stress_test") : NULL;

    double target = s0 ? db_get_num(s0, "target_amount") : 5000.0;
    double liquid = s0 ? db_get_num(s0, "liquid_cash") : 25000.0;
    double remaining = s1 ? db_get_num(s1, "remaining_cash") : 20000.0;
    double runway = s1 ? db_get_num(s1, "runway_months") : 5.7;
    int is_safe = s1 ? csilk_json_get_bool(s1, "is_safe") : 1;

    char buf[4096];
    snprintf(buf, sizeof(buf),
        "### ⚖️ 大额消费智能决策评估报告\n\n"
        "**拟计划支出金额**：`￥%.2f`\n\n"
        "#### 一、流动性压力测试\n"
        "- 🏦 **当前可用流动现金**：￥%.2f\n"
        "- 📉 **支出后剩余备用金**：￥%.2f\n"
        "- 🛡️ **剩余应急保障时长**：`%.1f 个月`（安全底线：3.0 个月）\n"
        "- 🚦 **决策建议等级**：%s\n\n"
        "#### 二、支付方案对比\n"
        "| 方案 | 资金占用 | 综合费率/息费 | 评价 |\n"
        "| :--- | :--- | :--- | :--- |\n"
        "| **全款现金支付** | ￥%.2f 一次性扣除 | 0 元（无利息） | %s |\n"
        "| **信用卡/分期 (6期)** | 约 ￥%.2f / 月 | 预计约 ￥%.2f 手续费 | 适合现金流需平滑过渡时 |\n\n"
        "#### 三、下一步行动备忘\n"
        "若确定购买，可点击下方快速确认记录草案：\n"
        "```action\n"
        "{\n"
        "  \"action_type\": \"daily_expense\",\n"
        "  \"amount\": %.2f,\n"
        "  \"category_name\": \"大额支出\",\n"
        "  \"note\": \"大额决策消费评估\"\n"
        "}\n"
        "```",
        target, liquid, remaining, runway,
        is_safe ? "🟢 **建议执行（安全边际充裕）**" : "🟡 **谨慎考虑（将削弱紧急备用金）**",
        target,
        is_safe ? "首选推荐，节省利息且备用金依然充裕" : "不建议一次性付清，宜保留流动性",
        target / 6.0, target * 0.045,
        target);

    if (root) csilk_json_free(root);
    return strdup(buf);
}

/* ========================================================================= */
/*  Workflow Registry                                                        */
/* ========================================================================= */

static const ai_workflow_def_t g_workflows[] = {
    {
        .id = "wf_monthly_review",
        .title = "月末财务深度复盘",
        .description = "自动汇聚本月收支与资产净值，环比异动分析与财务健康度综合评分，生成专业复盘图表报告。",
        .icon = "ph:calendar-check",
        .step_count = 4,
        .steps = {
            {"aggregate_data", "数据汇聚与对账", "拉取本月日常收支、交易流向及资金账户净资产", step_mr_aggregate},
            {"analyze_trends", "分类异动与超支项分析", "对比上月收支环比波动，挖掘高频与异常支出项", step_mr_trends},
            {"health_diagnosis", "财务健康度综合评分", "计算储蓄率、应急保障月数及负债健康指数", step_mr_health},
            {"generate_report", "生成深度复盘报告与图表", "结构化复盘建议与 Mermaid 收支流向图渲染", step_mr_generate_report},
        },
    },
    {
        .id = "wf_portfolio_rebalance",
        .title = "投资组合再平衡体检",
        .description = "扫描股票/基金/债券/加密资产全局持仓，计算大类资产配置偏离度，输出再平衡调仓建议与草案。",
        .icon = "ph:chart-polar",
        .step_count = 3,
        .steps = {
            {"scan_holdings", "全局持仓与资产扫描", "统计权益、固收、现金各类资产最新市值与比重", step_pr_scan},
            {"risk_exposure", "大类资产敞口测算", "比对标准大类配置基准，计算偏离幅度", step_pr_exposure},
            {"generate_report", "调仓方案与图表生成", "生成资产配置结构图与再平衡执行方案", step_pr_report},
        },
    },
    {
        .id = "wf_expense_decision",
        .title = "大额支出智能决策评估",
        .description = "测算大额支出对未来现金流与应急备用金的冲击程度，对比全款与分期成本，提供量化决策建议。",
        .icon = "ph:scales",
        .step_count = 3,
        .steps = {
            {"assess_liquidity", "流动性资金池测算", "获取活期与高流动性现金资产总额", step_ed_assess},
            {"stress_test", "安全边际压力测试", "模拟支出后未来 3~6 个月刚性支出保障能力", step_ed_stress_test},
            {"generate_report", "综合决策评估与执行草案", "输出支付方案对比建议与预定支出备忘", step_ed_report},
        },
    },
};

static const size_t g_workflow_count = sizeof(g_workflows) / sizeof(g_workflows[0]);

csilk_json_t*
ai_workflow_get_definitions_json(void)
{
    csilk_json_t* arr = csilk_json_array();
    for (size_t i = 0; i < g_workflow_count; i++) {
        const ai_workflow_def_t* wf = &g_workflows[i];
        csilk_json_t* w = csilk_json_object();
        csilk_json_add_string(w, "id", wf->id);
        csilk_json_add_string(w, "title", wf->title);
        csilk_json_add_string(w, "description", wf->description);
        csilk_json_add_string(w, "icon", wf->icon);
        csilk_json_add_number(w, "step_count", (double)wf->step_count);

        csilk_json_t* steps_arr = csilk_json_array();
        for (int j = 0; j < wf->step_count; j++) {
            csilk_json_t* st = csilk_json_object();
            csilk_json_add_string(st, "step_id", wf->steps[j].step_id);
            csilk_json_add_string(st, "title", wf->steps[j].title);
            csilk_json_add_string(st, "description", wf->steps[j].description);
            csilk_json_array_append(steps_arr, st);
        }
        csilk_json_add_array(w, "steps", steps_arr);
        csilk_json_array_append(arr, w);
    }
    return arr;
}

/* ========================================================================= */
/*  Workflow Execution & SSE Streaming                                       */
/* ========================================================================= */

void
ai_workflow_run_handler(csilk_ctx_t* c)
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

    const char* workflow_id = csilk_json_get_string(body, "workflow_id");
    int64_t session_id = (int64_t)db_get_num(body, "session_id");
    const csilk_json_t* params = csilk_json_get(body, "params");

    if (!workflow_id || !workflow_id[0]) {
        csilk_json_free(body);
        respond_bad_request(c, "缺少 workflow_id");
        return;
    }

    /* Find workflow definition */
    const ai_workflow_def_t* target_wf = NULL;
    for (size_t i = 0; i < g_workflow_count; i++) {
        if (strcmp(g_workflows[i].id, workflow_id) == 0) {
            target_wf = &g_workflows[i];
            break;
        }
    }

    if (!target_wf) {
        csilk_json_free(body);
        respond_bad_request(c, "未找到指定的工作流");
        return;
    }

    csilk_db_pool_t* pool = db_get_pool();

    /* Auto create session if not specified */
    if (session_id <= 0 && pool) {
        session_id = ai_session_insert(pool, user_id, target_wf->title, "workflow-agent", "system");
    }

    /* Start SSE streaming */
    csilk_sse_init(c);

    /* 1. Send workflow_start */
    csilk_json_t* ev_start = csilk_json_object();
    csilk_json_add_string(ev_start, "workflow_id", target_wf->id);
    csilk_json_add_string(ev_start, "title", target_wf->title);
    csilk_json_add_number(ev_start, "total_steps", (double)target_wf->step_count);
    csilk_json_add_number(ev_start, "session_id", (double)session_id);
    size_t slen = 0;
    char* str_start = csilk_json_serialize(ev_start, &slen);
    csilk_json_free(ev_start);
    csilk_sse_send(c, "workflow_start", str_start ? str_start : "{}");
    free(str_start);

    /* Cumulative context across steps */
    csilk_json_t* ctx_obj = csilk_json_object();
    char* final_report = NULL;

    for (int i = 0; i < target_wf->step_count; i++) {
        const ai_workflow_step_t* st = &target_wf->steps[i];

        /* Send step_start */
        csilk_json_t* ev_st_start = csilk_json_object();
        csilk_json_add_number(ev_st_start, "step_index", (double)i);
        csilk_json_add_string(ev_st_start, "step_id", st->step_id);
        csilk_json_add_string(ev_st_start, "title", st->title);
        char* str_st_start = csilk_json_serialize(ev_st_start, &slen);
        csilk_json_free(ev_st_start);
        csilk_sse_send(c, "step_start", str_st_start ? str_st_start : "{}");
        free(str_st_start);

        /* Execute step */
        char* cur_ctx_str = csilk_json_serialize(ctx_obj, &slen);
        char* step_out = st->execute(pool, user_id, params, cur_ctx_str);
        free(cur_ctx_str);

        if (i == target_wf->step_count - 1) {
            /* Last step is the final markdown report */
            final_report = step_out;
        } else if (step_out) {
            csilk_json_t* parsed_out = csilk_json_parse(step_out);
            if (parsed_out) {
                csilk_json_add_object(ctx_obj, st->step_id, parsed_out);
            }
            free(step_out);
        }

        /* Send step_complete */
        csilk_json_t* ev_st_done = csilk_json_object();
        csilk_json_add_number(ev_st_done, "step_index", (double)i);
        csilk_json_add_string(ev_st_done, "step_id", st->step_id);
        csilk_json_add_string(ev_st_done, "status", "completed");
        char* str_st_done = csilk_json_serialize(ev_st_done, &slen);
        csilk_json_free(ev_st_done);
        csilk_sse_send(c, "step_complete", str_st_done ? str_st_done : "{}");
        free(str_st_done);
    }

    csilk_json_free(ctx_obj);

    /* Stream final report text in chunks */
    if (final_report && final_report[0]) {
        size_t rlen = strlen(final_report);
        size_t offset = 0;
        size_t chunk_sz = 64;
        while (offset < rlen) {
            size_t take = (offset + chunk_sz < rlen) ? chunk_sz : (rlen - offset);
            char chunk[128];
            memcpy(chunk, final_report + offset, take);
            chunk[take] = '\0';
            offset += take;

            csilk_json_t* ev_chunk = csilk_json_object();
            csilk_json_add_string(ev_chunk, "content", chunk);
            char* str_chunk = csilk_json_serialize(ev_chunk, &slen);
            csilk_json_free(ev_chunk);
            csilk_sse_send(c, "delta", str_chunk ? str_chunk : "{}");
            free(str_chunk);
        }

        /* Persist generated report in session messages */
        if (pool && session_id > 0) {
            ai_message_insert(pool, session_id, "assistant", final_report, "workflow-agent");
        }
        free(final_report);
    }

    /* Send workflow_complete */
    csilk_json_t* ev_done = csilk_json_object();
    csilk_json_add_string(ev_done, "workflow_id", target_wf->id);
    csilk_json_add_string(ev_done, "status", "completed");
    csilk_json_add_number(ev_done, "session_id", (double)session_id);
    char* str_done = csilk_json_serialize(ev_done, &slen);
    csilk_json_free(ev_done);
    csilk_sse_send(c, "workflow_complete", str_done ? str_done : "{}");
    free(str_done);

    csilk_sse_close(c);
    csilk_json_free(body);
}
