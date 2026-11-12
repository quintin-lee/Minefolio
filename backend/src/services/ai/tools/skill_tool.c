#include "services/ai/tools/skill_tool.h"
#include "services/ai/tools/registry.h"
#include "services/ai/tools/schema.h"
#include "infrastructure/repositories/skill_repo_impl.h"
#include "application/skill/usecases.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char*
exec_use_skill(const ai_tool_t* tool, const ai_tool_context_t* ctx, const csilk_json_t* args)
{
    (void)tool;
    if (!ctx || !ctx->pool || ctx->user_id <= 0) {
        return strdup("{\"error\":\"invalid context\"}");
    }

    const char* skill_name = args ? csilk_json_get_string(args, "skill_name") : NULL;
    if (!skill_name || !skill_name[0]) {
        return strdup("{\"error\":\"skill_name is required\"}");
    }

    const char* skill_args = args ? csilk_json_get_string(args, "arguments") : NULL;

    csilk_json_t* sk =
        skill_repo_get_by_name((csilk_db_pool_t*)ctx->pool, ctx->user_id, skill_name);
    if (!sk) {
        char err[128];
        snprintf(err, sizeof(err), "{\"error\":\"Skill '%s' not found\"}", skill_name);
        return strdup(err);
    }

    int enabled = db_get_int(sk, "enabled");
    if (enabled == 0) {
        csilk_json_free(sk);
        char err[128];
        snprintf(err, sizeof(err), "{\"error\":\"Skill '%s' is disabled\"}", skill_name);
        return strdup(err);
    }

    int64_t id = (int64_t)db_get_num(sk, "id");
    csilk_json_free(sk);

    csilk_json_t* res = NULL;
    int           rc = skill_usecase_test_execute(ctx->pool, ctx->user_id, id, skill_args, &res);
    if (rc != 0 || !res) {
        return strdup("{\"error\":\"Failed to execute skill\"}");
    }

    size_t len = 0;
    char*  s = csilk_json_serialize(res, &len);
    csilk_json_free(res);
    return s;
}

static char*
exec_list_skills(const ai_tool_t* tool, const ai_tool_context_t* ctx, const csilk_json_t* args)
{
    (void)tool;
    (void)args;
    if (!ctx || !ctx->pool || ctx->user_id <= 0) {
        return strdup("{\"error\":\"invalid context\"}");
    }

    csilk_json_t* list = NULL;
    int           rc = skill_usecase_list(ctx->pool, ctx->user_id, NULL, 1, &list);
    if (rc != 0 || !list) {
        return strdup("{\"skills\":[]}");
    }

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_array(res, "skills", list);
    size_t len = 0;
    char*  s = csilk_json_serialize(res, &len);
    csilk_json_free(res);
    return s;
}

void
ai_tool_skill_register_all(void)
{
    /* 1. use_skill */
    {
        csilk_json_t* s = ai_schema_create_object();
        ai_schema_add_prop(s,
                           "skill_name",
                           "string",
                           "要调用的技能名称（kebab-case，如 financial-health-auditor）");
        ai_schema_add_prop(s, "arguments", "string", "传递给技能的可选参数或上下文 JSON 字符串");
        ai_schema_add_required(s, "skill_name");

        ai_tool_t t = {
            .name = "use_skill",
            .description =
                "加载并调用已启用的领域专属技能（Agent Skill），获取技能的规则指令或运算结果",
            .parameters_schema = s,
            .permission = AI_PERM_READ,
            .risk = AI_RISK_LOW,
            .is_mutation = false,
            .validate = NULL,
            .execute = exec_use_skill,
        };
        ai_tool_register(&t);
    }

    /* 2. list_skills */
    {
        csilk_json_t* s = ai_schema_create_object();

        ai_tool_t t = {
            .name = "list_skills",
            .description = "列出当前用户已启用的所有 AI 专属技能及其描述和分类",
            .parameters_schema = s,
            .permission = AI_PERM_READ,
            .risk = AI_RISK_LOW,
            .is_mutation = false,
            .validate = NULL,
            .execute = exec_list_skills,
        };
        ai_tool_register(&t);
    }
}
