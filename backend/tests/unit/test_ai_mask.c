#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "csilk/csilk.h"
#include "services/ai/tools/mask.h"

static void
expect_pii(const char* in, const char* expected)
{
    size_t out_len = strlen(in) + 1;
    char* out = (char*)malloc(out_len);
    assert(out);
    ai_mask_pii_text(in, out, out_len);
    if (strcmp(out, expected) != 0) {
        fprintf(stderr,
                "PII mismatch:\n  in      = %s\n  out     = %s\n  expected= %s\n",
                in, out, expected);
    }
    assert(strcmp(out, expected) == 0);
    free(out);
}

int
main(void)
{
    printf("Running test_ai_mask...\n");

    /* 银行卡：连续 16 位，保留末 4 */
    expect_pii("卡号 6222021234567890 尾", "卡号 ************7890 尾");
    /* 银行卡：4 位分组（4×4=16），保留末 4 */
    expect_pii("6222 0212 3456 7890", "**** **** **** 7890");
    /* 手机：11 位 13x 开头，保留前 3 + 末 4 */
    expect_pii("电话 13812345678", "电话 138****5678");
    /* 身份证 18 位：保留前 6 + 末 4 */
    expect_pii("110101199001011234", "110101********1234");
    /* 日期不误伤 */
    expect_pii("2026-11-22 12:34:56", "2026-11-22 12:34:56");
    /* 邮箱：local 保留首字符（domain 保留） */
    expect_pii("联系 a@b.com 或 x.y@ex.org", "联系 a@b.com 或 x**@ex.org");

    /* 键名黑名单：user_id 被丢弃，draft_token 保留（opaque 不脱敏） */
    {
        char* masked =
            ai_mask_tool_result("propose_daily_expense",
                                "{\"user_id\":7,\"draft_token\":\"abc123\",\"amount\":100}");
        assert(masked);
        csilk_json_t* j = csilk_json_parse(masked);
        assert(j);
        assert(csilk_json_get(j, "user_id") == NULL);
        assert(csilk_json_get(j, "draft_token"));
        assert(strcmp(csilk_json_get_string(j, "draft_token"), "abc123") == 0);
        csilk_json_free(j);
        free(masked);
    }

    /* 金额分级：默认保留；MINEFOLIO_AI_MASK_AMOUNTS=1 时量化到 1 位有效数字 */
    {
        const char* raw = "{\"total_assets\":600000.0,\"current_value\":1.2,\"savings_rate_pct\":10.5}";
        setenv("MINEFOLIO_AI_MASK_AMOUNTS", "0", 1);
        char* m0 = ai_mask_tool_result("get_summary", raw);
        assert(m0);
        csilk_json_t* j0 = csilk_json_parse(m0);
        assert(j0);
        assert(fabs(csilk_json_get_number(j0, "total_assets") - 600000.0) < 1e-6);
        csilk_json_free(j0);
        free(m0);

        setenv("MINEFOLIO_AI_MASK_AMOUNTS", "1", 1);
        char* m1 = ai_mask_tool_result("get_summary", raw);
        assert(m1);
        csilk_json_t* j1 = csilk_json_parse(m1);
        assert(j1);
        double ta = csilk_json_get_number(j1, "total_assets");
        assert(ta == 600000.0 || ta == 600000.0); /* 1 位有效数字 */
        double cv = csilk_json_get_number(j1, "current_value");
        assert(cv == 1.0 || cv == 2.0); /* 1.2 → 1 */
        /* pct 不量化 */
        assert(fabs(csilk_json_get_number(j1, "savings_rate_pct") - 10.5) < 1e-6);
        csilk_json_free(j1);
        free(m1);
        unsetenv("MINEFOLIO_AI_MASK_AMOUNTS");
    }

    /* 无 PII 且默认金额模式 → 逐字节不变 */
    {
        const char* raw = "{\"total\":3,\"assets\":[{\"name\":\"茅台\"}]}";
        char* m = ai_mask_tool_result("get_assets", raw);
        assert(m);
        assert(strcmp(m, raw) == 0);
        free(m);
    }

    printf("test_ai_mask passed\n");
    return 0;
}
