#include "services/ai/tools/mask.h"
#include "csilk/csilk.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* --------------------------------------------------------------------------
 * 键名策略
 * -------------------------------------------------------------------------- */

/* 身份键：从回灌 LLM 的结果中丢弃 */
static bool
key_is_dropped(const char* k)
{
    if (!k || !k[0]) {
        return false;
    }
    return strcasecmp(k, "user_id") == 0 || strcasecmp(k, "owner_user_id") == 0 ||
           strcasecmp(k, "member_user_id") == 0;
}

/* 不透明键：字符串值不做 PII 文本扫描（URL / 令牌 / 时间戳 / 代码等），
 * 避免掩码破坏可机读结构。draft_token 属于 *_token，必须跳过以免破坏 HMAC。 */
static bool
key_is_opaque(const char* k)
{
    if (!k || !k[0]) {
        return false;
    }
    if (strcasecmp(k, "token") == 0) {
        return true;
    }
    size_t n = strlen(k);
    if (n > 6 && strcasecmp(k + n - 6, "_token") == 0) {
        return true;
    }
    static const char* opaque_keys[] = {
        "url",
        "link",
        "href",
        "icon",
        "quote_source",
        "symbol",
        "currency",
        "locale",
        "timezone",
        "mermaid",
        "id",
        "created_at",
        "updated_at",
        "last_sync_at",
        "date",
        "expense_date",
        "transaction_date",
        "report_date",
    };
    for (size_t i = 0; i < sizeof(opaque_keys) / sizeof(opaque_keys[0]); ++i) {
        if (strcasecmp(k, opaque_keys[i]) == 0) {
            return true;
        }
    }
    return false;
}

/* 永远不视为金额（比率 / 计数 / 标识 / 行情） */
static bool
key_is_never_amount(const char* k)
{
    if (!k || !k[0]) {
        return false;
    }
    if (strcasecmp(k, "id") == 0 || strcasecmp(k, "total") == 0 || strcasecmp(k, "close") == 0 ||
        strcasecmp(k, "open") == 0 || strcasecmp(k, "high") == 0 || strcasecmp(k, "low") == 0 ||
        strcasecmp(k, "volume") == 0 || strcasecmp(k, "price") == 0 ||
        strcasecmp(k, "currency") == 0 || strcasecmp(k, "status") == 0 ||
        strcasecmp(k, "grade") == 0 || strcasecmp(k, "version") == 0 ||
        strcasecmp(k, "sort_order") == 0) {
        return true;
    }
    static const char* never_sub[] = {"pct",
                                      "percent",
                                      "ratio",
                                      "rate",
                                      "count",
                                      "score",
                                      "months",
                                      "days",
                                      "year",
                                      "quantity",
                                      "qty",
                                      "weight",
                                      "page",
                                      "size"};
    for (size_t i = 0; i < sizeof(never_sub) / sizeof(never_sub[0]); ++i) {
        if (strstr(k, never_sub[i])) {
            return true;
        }
    }
    size_t n = strlen(k);
    if (n > 3 && strcasecmp(k + n - 3, "_id") == 0) {
        return true;
    }
    return false;
}

/* 金额键：精确匹配 + total_ 前缀 + 金额类后缀 */
static bool
key_is_amount(const char* k)
{
    if (!k || !k[0] || key_is_never_amount(k)) {
        return false;
    }
    static const char* exact[] = {"amount",
                                  "balance",
                                  "balance_after",
                                  "delta",
                                  "cost_basis",
                                  "current_value",
                                  "net_value",
                                  "net_worth",
                                  "liquid_cash",
                                  "avg_burn",
                                  "avg_income",
                                  "monthly_income",
                                  "monthly_expense",
                                  "net_savings",
                                  "invested_assets",
                                  "final_balance",
                                  "total_principal",
                                  "total_interest",
                                  "monthly_payment",
                                  "total_repayment",
                                  "loan_amount",
                                  "principal",
                                  "interest",
                                  "gap",
                                  "target_amount",
                                  "income",
                                  "expense",
                                  "savings",
                                  "total_income",
                                  "total_expense",
                                  "total_assets",
                                  "total_liabilities",
                                  "total_debt",
                                  "total_market_value",
                                  "total_cost_basis",
                                  "total_floating_pnl",
                                  "total_realized_pnl",
                                  "floating_pnl",
                                  "realized_pnl",
                                  "market_value",
                                  "unrealized_pnl"};
    for (size_t i = 0; i < sizeof(exact) / sizeof(exact[0]); ++i) {
        if (strcasecmp(k, exact[i]) == 0) {
            return true;
        }
    }
    if (strncasecmp(k, "total_", 6) == 0) {
        return true;
    }
    size_t             n = strlen(k);
    static const char* suffixes[] = {"_amount",
                                     "_balance",
                                     "_value",
                                     "_cost",
                                     "_income",
                                     "_expense",
                                     "_pnl",
                                     "_cash",
                                     "_debt",
                                     "_assets",
                                     "_liabilities",
                                     "_burn",
                                     "_savings",
                                     "_payment",
                                     "_interest",
                                     "_principal",
                                     "_repayment",
                                     "_worth"};
    for (size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); ++i) {
        size_t slen = strlen(suffixes[i]);
        if (n > slen && strcasecmp(k + n - slen, suffixes[i]) == 0) {
            return true;
        }
    }
    return false;
}

/* --------------------------------------------------------------------------
 * 金额量化：1 位有效数字（数量级化）
 * -------------------------------------------------------------------------- */

static double
quantize_amount(double v)
{
    if (!isfinite(v) || v == 0.0) {
        return v;
    }
    double a = fabs(v);
    double p = pow(10.0, floor(log10(a)));
    double q = round(a / p) * p;
    return (v < 0.0) ? -q : q;
}

/* --------------------------------------------------------------------------
 * PII 文本脱敏（等长掩码）
 * -------------------------------------------------------------------------- */

/* 邮箱：保留 local part 首字符，其余（含 @ 后域名）掩码 */
static void
mask_emails(char* s, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        if (s[i] != '@') {
            continue;
        }
        /* 向前回溯 local part */
        size_t j = i;
        while (j > 0 && (isalnum((unsigned char)s[j - 1]) || s[j - 1] == '.' || s[j - 1] == '_' ||
                         s[j - 1] == '%' || s[j - 1] == '+' || s[j - 1] == '-')) {
            --j;
        }
        if (i - j < 2) {
            continue; /* 至少 2 个字符才判定为邮箱 */
        }
        /* @ 后首字符须为字母数字 */
        if (i + 1 < len && !isalnum((unsigned char)s[i + 1])) {
            continue;
        }
        for (size_t k = j + 1; k < i; ++k) {
            s[k] = '*';
        }
    }
}

/* 数字串：银行卡 / 手机 / 身份证（等长掩码，保留长度） */
static void
mask_digit_runs(char* s, size_t len)
{
    size_t i = 0;
    while (i < len) {
        if (!isdigit((unsigned char)s[i])) {
            ++i;
            continue;
        }
        /* 收集数字组：允许空格 / 短横作组间分隔，小数点不作为分隔 */
        size_t start = i;
        int    groups[8];
        int    group_count = 0;
        int    digits = 0;
        for (;;) {
            if (i < len && isdigit((unsigned char)s[i])) {
                int group_len = 0;
                while (i < len && isdigit((unsigned char)s[i])) {
                    ++i;
                    ++group_len;
                    ++digits;
                }
                if (group_count < 8) {
                    groups[group_count] = group_len;
                }
                ++group_count;
            }
            /* 组间分隔符：仅当其后紧跟数字组时才算分隔 */
            if (i < len && (s[i] == ' ' || s[i] == '-') && i + 1 < len &&
                isdigit((unsigned char)s[i + 1])) {
                ++i;
                continue;
            }
            break;
        }
        size_t span = i - start;

        int    g = group_count;
        size_t d = (size_t)digits;

        /* 判定（优先级：身份证 > 手机 > 银行卡） */
        bool has_x = false;
        if (i < len && (s[i] == 'X' || s[i] == 'x')) {
            has_x = true;
        }
        bool is_id = (g == 1 && d == 18) || (g == 1 && d == 17 && has_x);
        bool is_phone = (g == 1 && d == 11 && s[start] == '1' && start + 1 < len &&
                         s[start + 1] >= '3' && s[start + 1] <= '9');
        bool is_card = false;
        if (!is_id && !is_phone && d >= 13 && d <= 19) {
            if (g == 1) {
                is_card = true;
            } else if (g >= 4) {
                is_card = true;
                for (int k = 0; k < g; ++k) {
                    if (groups[k] != 4) {
                        is_card = false;
                        break;
                    }
                }
            }
        }

        if (is_id || is_phone || is_card) {
            int keep_head, keep_tail;
            if (is_id) {
                keep_head = 6;
                keep_tail = 4;
            } else if (is_phone) {
                keep_head = 3;
                keep_tail = 4;
            } else {
                keep_head = 0;
                keep_tail = 4;
            }
            int visible = (int)d + (is_id && has_x ? 1 : 0); /* 可被掩码的字符数 */
            int lo = keep_head;
            int hi = visible - keep_tail;
            if (hi < lo) {
                hi = lo;
            }
            int idx = 0;
            for (size_t p = start; p < start + span && p < len; ++p) {
                bool is_maskable = isdigit((unsigned char)s[p]);
                if (is_id && has_x && idx == (int)d) {
                    is_maskable = (s[p] == 'X' || s[p] == 'x');
                }
                if (is_maskable) {
                    if (idx >= lo && idx < hi) {
                        s[p] = '*';
                    }
                    ++idx;
                }
            }
            if (has_x && is_id) {
                ++i; /* X/x 属于号码一部分 */
            }
        }
        if (i == start) {
            ++i;
        }
    }
}

void
ai_mask_pii_text(const char* in, char* out, size_t out_len)
{
    if (!out || out_len == 0) {
        return;
    }
    if (!in) {
        out[0] = '\0';
        return;
    }
    size_t n = strlen(in);
    if (n + 1 > out_len) {
        n = out_len - 1;
    }
    memcpy(out, in, n);
    out[n] = '\0';
    mask_emails(out, n);
    mask_digit_runs(out, n);
}

/* --------------------------------------------------------------------------
 * JSON 重建树
 * -------------------------------------------------------------------------- */

static void
attach_node(csilk_json_t* dst, const char* key, csilk_json_t* child, bool* changed)
{
    if (!dst || !child) {
        return;
    }
    if (csilk_json_is_object(child)) {
        csilk_json_add_object(dst, key, child);
    } else if (csilk_json_is_array(child)) {
        csilk_json_add_array(dst, key, child);
    } else if (csilk_json_is_string(child)) {
        if (!csilk_json_add_string(dst, key, csilk_json_string_value(child))) {
            csilk_json_free(child);
            return;
        }
        csilk_json_free(child);
    } else if (csilk_json_is_number(child)) {
        if (!csilk_json_add_number(dst, key, csilk_json_number_value(child))) {
            csilk_json_free(child);
            return;
        }
        csilk_json_free(child);
    } else if (csilk_json_is_bool(child)) {
        if (!csilk_json_add_bool(dst, key, csilk_json_bool_value(child))) {
            csilk_json_free(child);
            return;
        }
        csilk_json_free(child);
    } else { /* null */
        csilk_json_add_null(dst, key);
        csilk_json_free(child);
    }
}

/* 字符串节点：opaque 键保留原值；否则 PII 脱敏（长度可能改变则标记 changed） */
static csilk_json_t*
mask_string_node(const csilk_json_t* v, const char* key, bool* changed)
{
    const char* s = csilk_json_string_value(v);
    if (!key || !key_is_opaque(key)) {
        size_t len = s ? strlen(s) : 0;
        char*  buf = (char*)malloc(len + 1);
        if (buf) {
            ai_mask_pii_text(s, buf, len + 1);
            if (strcmp(buf, s) != 0) {
                *changed = true;
            }
            csilk_json_t* out = csilk_json_string_new(buf);
            free(buf);
            return out;
        }
    }
    /* opaque 或无法分配：在目标文档内新建字符串节点（值拷贝）。
     * 注意：绝不能用 csilk_json_copy(v) —— v 属于源文档 parsed，跨文档挂接会破坏
     * yyjson 内部结构导致 serialize 失败。mask_node 先于 free(parsed) 执行，s 仍有效。 */
    return csilk_json_string_new(s ? s : "");
}

/* 数值节点：金额模式且为金额键则量化 */
static csilk_json_t*
mask_number_node(const csilk_json_t* v, const char* key, bool amounts, bool* changed)
{
    double val = csilk_json_number_value(v);
    if (amounts && key && key_is_amount(key)) {
        double q = quantize_amount(val);
        if (q != val) {
            *changed = true;
        }
        return csilk_json_number(q);
    }
    return csilk_json_number(val);
}

static csilk_json_t*
mask_node(const csilk_json_t* v, const char* key, bool amounts, bool* changed)
{
    if (!v) {
        return csilk_json_null();
    }
    if (csilk_json_is_object(v)) {
        csilk_json_t* out = csilk_json_object();
        size_t        size = csilk_json_object_size(v);
        for (size_t i = 0; i < size; ++i) {
            const char*   k = csilk_json_object_key(v, i);
            csilk_json_t* child = csilk_json_object_val(v, i);
            if (!k) {
                continue;
            }
            if (key_is_dropped(k)) {
                *changed = true;
                continue;
            }
            /* k 是源文档 v 的借用指针（csilk add_* 不深拷键，序列化前依赖其存活）。
             * 直接借用、不 strdup/不 free；mask_json_or_text 保证在 free(parsed) 前
             * 完成 serialize，键名在此期间始终有效。 */
            csilk_json_t* masked_child = mask_node(child, k, amounts, changed);
            attach_node(out, k, masked_child, changed);
        }
        return out;
    }
    if (csilk_json_is_array(v)) {
        csilk_json_t* out = csilk_json_array();
        size_t        size = csilk_json_array_size(v);
        for (size_t i = 0; i < size; ++i) {
            csilk_json_t* elem = csilk_json_array_get(v, i);
            csilk_json_t* masked_elem = mask_node(elem, key, amounts, changed);
            if (masked_elem) {
                csilk_json_array_append(out, masked_elem);
            }
        }
        return out;
    }
    if (csilk_json_is_string(v)) {
        return mask_string_node(v, key, changed);
    }
    if (csilk_json_is_number(v)) {
        return mask_number_node(v, key, amounts, changed);
    }
    if (csilk_json_is_bool(v)) {
        return csilk_json_bool(csilk_json_bool_value(v));
    }
    /* null / 其他 */
    return csilk_json_null();
}

/* --------------------------------------------------------------------------
 * 对外接口
 * -------------------------------------------------------------------------- */

static bool
is_skip_tool(const char* tool_name)
{
    if (!tool_name || tool_name[0] == '\0') {
        return false;
    }
    static const char* skip[] = {
        "get_current_time",
        "calculate_date_range",
        "get_exchange_rate",
        "web_search",
        "fetch_webpage",
        "list_skills",
        "use_skill",
    };
    for (size_t i = 0; i < sizeof(skip) / sizeof(skip[0]); ++i) {
        if (strcmp(tool_name, skip[i]) == 0) {
            return true;
        }
    }
    return false;
}

bool
ai_mask_amounts_enabled(void)
{
    const char* v = getenv("MINEFOLIO_AI_MASK_AMOUNTS");
    return v && strcmp(v, "1") == 0;
}

static char*
mask_json_or_text(const char* raw, bool amounts)
{
    if (!raw) {
        return NULL;
    }
    csilk_json_t* parsed = csilk_json_parse(raw);
    if (parsed) {
        bool          changed = false;
        csilk_json_t* masked = mask_node(parsed, NULL, amounts, &changed);
        /* 序列化必须在 free(parsed) 之前完成：out 的键借用 parsed 的内存，
         * 提前释放会让键名悬空、serialize 返回 NULL。 */
        size_t len = 0;
        char*  out = csilk_json_serialize(masked, &len);
        csilk_json_free(masked);
        csilk_json_free(parsed);
        if (!out) {
            return strdup(raw);
        }
        /* 未产生任何变化 → 返回逐字节一致副本，避免下游 JSON 比较漂移 */
        if (!changed) {
            free(out);
            return strdup(raw);
        }
        return out;
    }
    /* 非 JSON（纯文本 / MCP 原始返回）→ 整体 PII 脱敏 */
    size_t len = strlen(raw);
    char*  out = (char*)malloc(len + 1);
    if (!out) {
        return NULL;
    }
    ai_mask_pii_text(raw, out, len + 1);
    return out;
}

char*
ai_mask_tool_result(const char* tool_name, const char* raw_json)
{
    if (!raw_json) {
        return NULL;
    }
    if (is_skip_tool(tool_name)) {
        return strdup(raw_json);
    }
    return mask_json_or_text(raw_json, ai_mask_amounts_enabled());
}

char*
ai_mask_workflow_context(const char* workflow_id, const char* json)
{
    (void)workflow_id;
    if (!json) {
        return NULL;
    }
    return mask_json_or_text(json, ai_mask_amounts_enabled());
}
