#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <unistd.h>

#include "csilk/csilk.h"
#include "services/ai/policy/risk.h"
#include "services/ai/policy/permission.h"
#include "services/ai/policy/confirmation.h"
#include "services/ai/policy/policy.h"
#include "services/ai/policy/audit.h"
#include "common/db.h"

int
main(void)
{
    printf("====================================================\n");
    printf("  AI Policy / Risk / Confirmation Framework Tests   \n");
    printf("====================================================\n");

    /* 0. 准备测试密钥与基础环境 */
    const char* secret_a = "test_ai_secret_key_alpha_2026";
    const char* secret_b = "test_ai_secret_key_beta_diff";
    ai_confirmation_set_secret(secret_a);

    /* ------------------------------------------------------------- */
    /* Test 1: Constant-Time Comparison Verification                 */
    /* ------------------------------------------------------------- */
    printf("[Test 1] Constant-time comparison...\n");
    {
        char buf1[] = "hello_secret_12345";
        char buf2[] = "hello_secret_12345";
        char buf3[] = "hello_secret_12349";

        assert(ai_confirmation_constant_time_memcmp(buf1, buf2, strlen(buf1)) == 0);
        assert(ai_confirmation_constant_time_memcmp(buf1, buf3, strlen(buf1)) != 0);
        printf("  ✅ Constant-time comparison matches correctly\n");
    }

    /* ------------------------------------------------------------- */
    /* Test 2: Normal Token Creation & Verification                  */
    /* ------------------------------------------------------------- */
    printf("[Test 2] Normal Token Creation & Verification...\n");
    {
        int64_t user_id = 1001;
        int64_t session_id = 42;
        const char* tool = "confirm_proposed_expense";

        csilk_json_t* args = csilk_json_object();
        csilk_json_add_number(args, "amount", 120.50);
        csilk_json_add_string(args, "category", "餐饮");

        char* token = ai_confirmation_create_bound_token(
            user_id, session_id, tool, args, AI_RISK_HIGH, 300);
        assert(token != NULL);
        assert(strncmp(token, "mf_v2.", 6) == 0);

        /* 正常校验 */
        ai_confirmation_status_t st =
            ai_confirmation_verify_and_consume(user_id, session_id, tool, args, token);
        assert(st == AI_CONFIRM_OK);
        printf("  ✅ Normal token verified and consumed\n");

        free(token);
        csilk_json_free(args);
    }

    /* ------------------------------------------------------------- */
    /* Test 3: Replay & Double Execution Attack Protection           */
    /* ------------------------------------------------------------- */
    printf("[Test 3] Replay & Double Execution Attack Protection...\n");
    {
        int64_t user_id = 1001;
        int64_t session_id = 42;
        const char* tool = "confirm_proposed_expense";

        csilk_json_t* args = csilk_json_object();
        csilk_json_add_number(args, "amount", 500.0);

        char* token = ai_confirmation_create_bound_token(
            user_id, session_id, tool, args, AI_RISK_HIGH, 300);
        assert(token != NULL);

        /* 第一次消费成功 */
        ai_confirmation_status_t st1 =
            ai_confirmation_verify_and_consume(user_id, session_id, tool, args, token);
        assert(st1 == AI_CONFIRM_OK);

        /* 第二次使用相同 token (Replay / Double Execution) 必须被拒绝 */
        ai_confirmation_status_t st2 =
            ai_confirmation_verify_and_consume(user_id, session_id, tool, args, token);
        assert(st2 == AI_CONFIRM_ERR_REPLAY);
        printf("  ✅ Double execution / replay blocked: %s\n", ai_confirmation_strerror(st2));

        free(token);
        csilk_json_free(args);
    }

    /* ------------------------------------------------------------- */
    /* Test 4: Expired Confirmation Token                            */
    /* ------------------------------------------------------------- */
    printf("[Test 4] Expired Confirmation Token...\n");
    {
        int64_t user_id = 1001;
        int64_t session_id = 42;
        const char* tool = "confirm_proposed_expense";

        csilk_json_t* args = csilk_json_object();
        csilk_json_add_number(args, "amount", 88.0);

        /* 创建一个已过期的 token (TTL = -10 秒) */
        char* token = ai_confirmation_create_bound_token(
            user_id, session_id, tool, args, AI_RISK_HIGH, -10);
        assert(token != NULL);

        ai_confirmation_status_t st =
            ai_confirmation_verify_and_consume(user_id, session_id, tool, args, token);
        assert(st == AI_CONFIRM_ERR_EXPIRED);
        printf("  ✅ Expired token blocked: %s\n", ai_confirmation_strerror(st));

        free(token);
        csilk_json_free(args);
    }

    /* ------------------------------------------------------------- */
    /* Test 5: Modified Argument Tamper-Proofing                     */
    /* ------------------------------------------------------------- */
    printf("[Test 5] Modified Argument Tamper-Proofing...\n");
    {
        int64_t user_id = 1001;
        int64_t session_id = 42;
        const char* tool = "confirm_proposed_expense";

        csilk_json_t* args_original = csilk_json_object();
        csilk_json_add_number(args_original, "amount", 100.0);
        csilk_json_add_number(args_original, "asset_id", 10.0);

        char* token = ai_confirmation_create_bound_token(
            user_id, session_id, tool, args_original, AI_RISK_HIGH, 300);

        /* 攻击者篡改金额为 1000.0 */
        csilk_json_t* args_tampered = csilk_json_object();
        csilk_json_add_number(args_tampered, "amount", 1000.0);
        csilk_json_add_number(args_tampered, "asset_id", 10.0);

        ai_confirmation_status_t st =
            ai_confirmation_verify_and_consume(user_id, session_id, tool, args_tampered, token);
        assert(st == AI_CONFIRM_ERR_ARGS_MISMATCH);
        printf("  ✅ Modified argument blocked: %s\n", ai_confirmation_strerror(st));

        free(token);
        csilk_json_free(args_original);
        csilk_json_free(args_tampered);
    }

    /* ------------------------------------------------------------- */
    /* Test 6: Modified User (Cross-User Impersonation)              */
    /* ------------------------------------------------------------- */
    printf("[Test 6] Modified User (Cross-User Impersonation)...\n");
    {
        int64_t victim_user = 1001;
        int64_t attacker_user = 2002;
        int64_t session_id = 42;
        const char* tool = "confirm_proposed_expense";

        csilk_json_t* args = csilk_json_object();
        csilk_json_add_number(args, "amount", 250.0);

        char* token = ai_confirmation_create_bound_token(
            victim_user, session_id, tool, args, AI_RISK_HIGH, 300);

        /* 攻击者尝试在自己的 session 中使用受害者的 token */
        ai_confirmation_status_t st =
            ai_confirmation_verify_and_consume(attacker_user, session_id, tool, args, token);
        assert(st == AI_CONFIRM_ERR_USER_MISMATCH);
        printf("  ✅ Cross-user attack blocked: %s\n", ai_confirmation_strerror(st));

        free(token);
        csilk_json_free(args);
    }

    /* ------------------------------------------------------------- */
    /* Test 7: Modified Tool (Cross-Tool Confusion Attack)           */
    /* ------------------------------------------------------------- */
    printf("[Test 7] Modified Tool (Cross-Tool Confusion Attack)...\n");
    {
        int64_t user_id = 1001;
        int64_t session_id = 42;

        csilk_json_t* args = csilk_json_object();
        csilk_json_add_number(args, "amount", 300.0);

        /* 授权工具为 expense */
        char* token = ai_confirmation_create_bound_token(
            user_id, session_id, "confirm_proposed_expense", args, AI_RISK_HIGH, 300);

        /* 攻击者尝试将 token 移花接木至 transfer 工具 */
        ai_confirmation_status_t st =
            ai_confirmation_verify_and_consume(user_id, session_id, "confirm_proposed_transfer", args, token);
        assert(st == AI_CONFIRM_ERR_TOOL_MISMATCH);
        printf("  ✅ Cross-tool attack blocked: %s\n", ai_confirmation_strerror(st));

        free(token);
        csilk_json_free(args);
    }

    /* ------------------------------------------------------------- */
    /* Test 8: Forged Signature (Tampered Token String)              */
    /* ------------------------------------------------------------- */
    printf("[Test 8] Forged Signature (Tampered Token String)...\n");
    {
        int64_t user_id = 1001;
        int64_t session_id = 42;
        const char* tool = "confirm_proposed_expense";

        csilk_json_t* args = csilk_json_object();
        csilk_json_add_number(args, "amount", 60.0);

        char* token = ai_confirmation_create_bound_token(
            user_id, session_id, tool, args, AI_RISK_HIGH, 300);

        /* 篡改签名字符串末尾 */
        size_t tlen = strlen(token);
        token[tlen - 1] = (token[tlen - 1] == 'a') ? 'b' : 'a';

        ai_confirmation_status_t st =
            ai_confirmation_verify_and_consume(user_id, session_id, tool, args, token);
        assert(st == AI_CONFIRM_ERR_SIGNATURE);
        printf("  ✅ Forged signature blocked: %s\n", ai_confirmation_strerror(st));

        free(token);
        csilk_json_free(args);
    }

    /* ------------------------------------------------------------- */
    /* Test 9: Wrong Secret Verification                             */
    /* ------------------------------------------------------------- */
    printf("[Test 9] Wrong Secret Verification...\n");
    {
        int64_t user_id = 1001;
        int64_t session_id = 42;
        const char* tool = "confirm_proposed_expense";

        csilk_json_t* args = csilk_json_object();
        csilk_json_add_number(args, "amount", 99.0);

        /* 使用 Secret A 签名 */
        ai_confirmation_set_secret(secret_a);
        char* token = ai_confirmation_create_bound_token(
            user_id, session_id, tool, args, AI_RISK_HIGH, 300);

        /* 切换至不同 Secret B 进行校验 */
        ai_confirmation_set_secret(secret_b);
        ai_confirmation_status_t st =
            ai_confirmation_verify_and_consume(user_id, session_id, tool, args, token);
        assert(st == AI_CONFIRM_ERR_SIGNATURE);
        printf("  ✅ Wrong secret rejected: %s\n", ai_confirmation_strerror(st));

        /* 还原密钥 */
        ai_confirmation_set_secret(secret_a);
        free(token);
        csilk_json_free(args);
    }

    /* ------------------------------------------------------------- */
    /* Test 10: Policy Engine Rate Limit (Frequency Control)         */
    /* ------------------------------------------------------------- */
    printf("[Test 10] Policy Engine Frequency Limit...\n");
    {
        ai_policy_rules_t custom = {
            .single_amount_limit = 50000.0,
            .large_amount_threshold = 10000.0,
            .max_frequency_per_minute = 5, /* 限制每分钟 5 次 */
            .enforce_confirmation = true,
        };
        ai_policy_set_rules(&custom);
        ai_policy_reset_frequency_limits();

        int64_t test_user = 999;
        csilk_json_t* args = csilk_json_object();
        csilk_json_add_number(args, "amount", 10.0);

        /* 前 5 次应该放行 */
        for (int i = 0; i < 5; i++) {
            ai_policy_decision_t* dec = ai_policy_evaluate(test_user, 1, "get_assets", args);
            assert(dec != NULL && dec->allowed == true);
            ai_policy_decision_free(dec);
        }

        /* 第 6 次超过阈值，必须被拦截 */
        ai_policy_decision_t* dec_blocked = ai_policy_evaluate(test_user, 1, "get_assets", args);
        assert(dec_blocked != NULL);
        assert(dec_blocked->allowed == false);
        assert(strstr(dec_blocked->reason, "Frequency limit exceeded") != NULL);
        printf("  ✅ Rate limit triggered on 6th request: %s\n", dec_blocked->reason);
        ai_policy_decision_free(dec_blocked);

        csilk_json_free(args);
    }

    /* ------------------------------------------------------------- */
    /* Test 11: Policy Engine Amount Limits & Escalation             */
    /* ------------------------------------------------------------- */
    printf("[Test 11] Policy Engine Amount Limits & Escalation...\n");
    {
        ai_policy_rules_t custom = {
            .single_amount_limit = 100000.0,  /* 最高上限 100,000 */
            .large_amount_threshold = 20000.0, /* 临界值 20,000 升级为 CRITICAL */
            .max_frequency_per_minute = 60,
            .enforce_confirmation = true,
        };
        ai_policy_set_rules(&custom);
        ai_policy_reset_frequency_limits();

        int64_t user = 1001;

        /* 11a: 超过单笔限额 100,000 */
        csilk_json_t* huge_args = csilk_json_object();
        csilk_json_add_number(huge_args, "amount", 150000.0);
        ai_policy_decision_t* dec_huge = ai_policy_evaluate(user, 1, "confirm_proposed_transfer", huge_args);
        assert(dec_huge != NULL);
        assert(dec_huge->allowed == false);
        assert(strstr(dec_huge->reason, "Amount limit exceeded") != NULL);
        printf("  ✅ Excess amount blocked: %s\n", dec_huge->reason);
        ai_policy_decision_free(dec_huge);
        csilk_json_free(huge_args);

        /* 11b: 金额 50,000，未超过最高上限，但超过 20,000 临界值，风险提升为 CRITICAL */
        csilk_json_t* large_args = csilk_json_object();
        csilk_json_add_number(large_args, "amount", 50000.0);
        ai_policy_decision_t* dec_large = ai_policy_evaluate(user, 1, "confirm_proposed_transfer", large_args);
        assert(dec_large != NULL);
        assert(dec_large->allowed == true);
        assert(dec_large->risk_level == AI_RISK_CRITICAL);
        printf("  ✅ Large amount dynamically escalated to %s risk\n",
               ai_risk_level_to_string(dec_large->risk_level));
        ai_policy_decision_free(dec_large);
        csilk_json_free(large_args);
    }

    /* ------------------------------------------------------------- */
    /* Test 12: Audit Logging Security (No Secret Leaks)             */
    /* ------------------------------------------------------------- */
    printf("[Test 12] Audit Logging Security (No Secret Leaks)...\n");
    {
        ai_audit_clear();

        ai_audit_record_t rec = {
            .stage = AI_AUDIT_STAGE_EXECUTION,
            .actor_id = 1001,
            .session_id = 42,
            .risk = AI_RISK_HIGH,
            .timestamp = (int64_t)time(NULL),
            .success = true,
        };
        strncpy(rec.tool, "confirm_proposed_expense", sizeof(rec.tool) - 1);
        strncpy(rec.arguments_hash, "f2ca1bb6c7e907d06dafe4687e579fce76b37e4e93b7605022da52e6ccc26fd2", sizeof(rec.arguments_hash) - 1);
        /* 模拟在 summary 中带有敏感 draft_token */
        snprintf(rec.result_summary, sizeof(rec.result_summary), "Success with draft_token mf_v2.xxx.yyy");

        ai_audit_log(&rec);

        size_t count = 0;
        const ai_audit_record_t* list = ai_audit_get_recent(&count);
        assert(count == 1);
        assert(strstr(list[0].result_summary, "mf_v2.") == NULL);
        assert(strstr(list[0].result_summary, "[REDACTED_SECRET]") != NULL);
        printf("  ✅ Sensitive tokens automatically redacted from audit logs\n");
    }

    printf("\n🎉 ALL AI POLICY / RISK / CONFIRMATION SECURITY TESTS PASSED!\n");
    return 0;
}
