#include "interfaces/http/controllers/skill_controller.h"
#include "application/skill/usecases.h"
#include "application/skill/dtos.h"
#include "application/task/async_task_manager.h"
#include "application/auth/usecases.h"
#include "common/response.h"
#include "common/ctx.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char*
body_get_str(const csilk_json_t* body, const char* key)
{
    const csilk_json_t* v = csilk_json_get(body, key);
    if (v && csilk_json_is_string(v)) {
        return csilk_json_string_value(v);
    }
    return NULL;
}

void
api_ai_skills_list(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    const char* category = csilk_get_query(c, "category");
    const char* en_str = csilk_get_query(c, "enabled_only");
    int         enabled_only = (en_str && strcmp(en_str, "1") == 0) ? 1 : 0;

    int64_t page = 1;
    int64_t page_size = 20;
    parse_page_params(c, &page, &page_size);

    csilk_json_t* data = NULL;
    int64_t       total = 0;
    int           rc = skill_usecase_list_paged(
        db_get_pool(), user_id, category, enabled_only, page, page_size, &data, &total);

    /* 懒加载预置技能：当用户未指定过滤条件且总数为 0 时，自动播种默认技能 */
    if (rc == 0 && total == 0 && (!category || !category[0]) && !enabled_only) {
        if (data) {
            csilk_json_free(data);
            data = NULL;
        }
        skill_usecase_seed_defaults(db_get_pool(), user_id);
        rc = skill_usecase_list_paged(
            db_get_pool(), user_id, category, enabled_only, page, page_size, &data, &total);
    }

    if (rc != 0 || !data) {
        respond_page_ok(c, csilk_json_array(), 0, page, page_size);
        return;
    }
    respond_page_ok(c, data, total, page, page_size);
}

void
api_ai_skills_get(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    int64_t     id = parse_query_int_strict(id_str, 0);
    if (id <= 0) {
        respond_error(c, 1002, "无效的技能 ID");
        return;
    }

    csilk_json_t* data = NULL;
    int           rc = skill_usecase_get(db_get_pool(), user_id, id, &data);
    if (rc != 0 || !data) {
        respond_error(c, 1002, "技能不存在");
        return;
    }
    respond_ok(c, data);
}

void
api_ai_skills_create(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_error(c, 1002, "请求体必须为合法 JSON");
        return;
    }

    const char* name = body_get_str(body, "name");
    const char* desc = body_get_str(body, "description");
    if (!name || !desc) {
        csilk_json_free(body);
        respond_error(c, 1002, "技能名称与描述为必填项");
        return;
    }

    mf_skill_create_dto_t dto = {0};
    strncpy(dto.name, name, sizeof(dto.name) - 1);
    strncpy(dto.description, desc, sizeof(dto.description) - 1);

    const char* lic = body_get_str(body, "license");
    if (lic) {
        strncpy(dto.license, lic, sizeof(dto.license) - 1);
    }

    const char* comp = body_get_str(body, "compatibility");
    if (comp) {
        strncpy(dto.compatibility, comp, sizeof(dto.compatibility) - 1);
    }

    const char* cat = body_get_str(body, "category");
    strncpy(dto.category, (cat && cat[0]) ? cat : "finance", sizeof(dto.category) - 1);

    const char* tags = body_get_str(body, "tags");
    if (tags) {
        strncpy(dto.tags, tags, sizeof(dto.tags) - 1);
    }

    dto.enabled = db_get_int(body, "enabled");
    if (csilk_json_get(body, "enabled") == NULL) {
        dto.enabled = 1;
    }

    const char* l2 = body_get_str(body, "instructions_l2");
    dto.instructions_l2 = l2 ? l2 : "";

    const char* bk = body_get_str(body, "backend");
    dto.backend =
        (bk && strcmp(bk, "remote") == 0) ? MF_SKILL_BACKEND_REMOTE : MF_SKILL_BACKEND_LOCAL;

    const char* root = body_get_str(body, "exec_root");
    if (root) {
        strncpy(dto.exec_cfg.root, root, sizeof(dto.exec_cfg.root) - 1);
    }

    const char* path = body_get_str(body, "exec_path");
    if (path) {
        strncpy(dto.exec_cfg.path, path, sizeof(dto.exec_cfg.path) - 1);
    }

    const char* args = body_get_str(body, "exec_args");
    if (args) {
        strncpy(dto.exec_cfg.args, args, sizeof(dto.exec_cfg.args) - 1);
    }

    const char* env = body_get_str(body, "exec_env");
    if (env) {
        strncpy(dto.exec_cfg.env, env, sizeof(dto.exec_cfg.env) - 1);
    }

    dto.exec_cfg.timeout_ms = db_get_int(body, "timeout_ms");
    dto.exec_cfg.max_output_bytes = db_get_int(body, "max_output_bytes");

    // Security gate: L3 process execution requires admin
    if (dto.exec_cfg.path[0] != '\0') {
        if (!auth_usecase_is_instance_admin(db_get_pool(), user_id)) {
            csilk_json_free(body);
            respond_forbidden(c, "仅实例管理员有权配置本地脚本进程执行");
            return;
        }
    }

    int64_t new_id = 0;
    int     rc = skill_usecase_create(db_get_pool(), user_id, &dto, &new_id);
    csilk_json_free(body);
    if (rc == 1004) {
        respond_error(c, 1004, "技能名称已存在");
        return;
    } else if (rc != 0) {
        respond_error(c, 1002, "参数校验失败，请检查名称格式（kebab-case）与路径");
        return;
    }

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_number(resp, "id", (double)new_id);
    respond_ok(c, resp);
}

void
api_ai_skills_update(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    int64_t     id = parse_query_int_strict(id_str, 0);
    if (id <= 0) {
        respond_error(c, 1002, "无效的技能 ID");
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_error(c, 1002, "请求体必须为合法 JSON");
        return;
    }

    const char* desc = body_get_str(body, "description");
    if (!desc) {
        csilk_json_free(body);
        respond_error(c, 1002, "技能描述不能为空");
        return;
    }

    mf_skill_update_dto_t dto = {0};
    strncpy(dto.description, desc, sizeof(dto.description) - 1);

    const char* lic = body_get_str(body, "license");
    if (lic) {
        strncpy(dto.license, lic, sizeof(dto.license) - 1);
    }

    const char* comp = body_get_str(body, "compatibility");
    if (comp) {
        strncpy(dto.compatibility, comp, sizeof(dto.compatibility) - 1);
    }

    const char* cat = body_get_str(body, "category");
    strncpy(dto.category, (cat && cat[0]) ? cat : "finance", sizeof(dto.category) - 1);

    const char* tags = body_get_str(body, "tags");
    if (tags) {
        strncpy(dto.tags, tags, sizeof(dto.tags) - 1);
    }

    if (csilk_json_get(body, "enabled")) {
        dto.has_enabled = 1;
        dto.enabled = (int)db_get_int(body, "enabled");
    }

    const char* l2 = body_get_str(body, "instructions_l2");
    dto.instructions_l2 = l2 ? l2 : "";

    const char* bk = body_get_str(body, "backend");
    dto.backend =
        (bk && strcmp(bk, "remote") == 0) ? MF_SKILL_BACKEND_REMOTE : MF_SKILL_BACKEND_LOCAL;

    const char* root = body_get_str(body, "exec_root");
    if (root) {
        strncpy(dto.exec_cfg.root, root, sizeof(dto.exec_cfg.root) - 1);
    }

    const char* path = body_get_str(body, "exec_path");
    if (path) {
        strncpy(dto.exec_cfg.path, path, sizeof(dto.exec_cfg.path) - 1);
    }

    const char* args = body_get_str(body, "exec_args");
    if (args) {
        strncpy(dto.exec_cfg.args, args, sizeof(dto.exec_cfg.args) - 1);
    }

    const char* env = body_get_str(body, "exec_env");
    if (env) {
        strncpy(dto.exec_cfg.env, env, sizeof(dto.exec_cfg.env) - 1);
    }

    dto.exec_cfg.timeout_ms = db_get_int(body, "timeout_ms");
    dto.exec_cfg.max_output_bytes = db_get_int(body, "max_output_bytes");

    if (dto.exec_cfg.path[0] != '\0') {
        if (!auth_usecase_is_instance_admin(db_get_pool(), user_id)) {
            csilk_json_free(body);
            respond_forbidden(c, "仅实例管理员有权配置本地脚本进程执行");
            return;
        }
    }

    int rc = skill_usecase_update(db_get_pool(), user_id, id, &dto);
    csilk_json_free(body);
    if (rc != 0) {
        respond_error(c, 1002, "更新失败，技能不存在或参数非法");
        return;
    }
    respond_ok(c, NULL);
}

void
api_ai_skills_delete(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    int64_t     id = parse_query_int_strict(id_str, 0);
    if (id <= 0) {
        respond_error(c, 1002, "无效的技能 ID");
        return;
    }

    int rc = skill_usecase_delete(db_get_pool(), user_id, id);
    if (rc != 0) {
        respond_error(c, 1002, "删除失败");
        return;
    }
    respond_ok(c, NULL);
}

void
api_ai_skills_toggle(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    int64_t     id = parse_query_int_strict(id_str, 0);
    if (id <= 0) {
        respond_error(c, 1002, "无效的技能 ID");
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    int           enabled = 1;
    if (body) {
        enabled = db_get_int(body, "enabled");
        csilk_json_free(body);
    }

    int rc = skill_usecase_toggle(db_get_pool(), user_id, id, enabled);
    if (rc != 0) {
        respond_error(c, 1002, "状态切换失败");
        return;
    }
    respond_ok(c, NULL);
}

typedef struct {
    int64_t skill_id;
    char*   args;
} skill_test_task_arg_t;

static void
skill_test_task_cleanup(void* raw)
{
    skill_test_task_arg_t* arg = (skill_test_task_arg_t*)raw;
    if (!arg) {
        return;
    }
    free(arg->args);
    free(arg);
}

static int
skill_test_task_fn(void*   pool,
                   int64_t task_id,
                   int64_t user_id,
                   int64_t ledger_id,
                   void*   arg,
                   char**  out_result_json,
                   char*   err_buf,
                   size_t  err_cap)
{
    (void)ledger_id;
    skill_test_task_arg_t* targ = (skill_test_task_arg_t*)arg;
    if (!targ || targ->skill_id <= 0) {
        snprintf(err_buf, err_cap, "参数错误");
        return -1;
    }

    mf_async_task_update_progress(pool, task_id, 20, "正在沙箱执行 L3 技能脚本");

    csilk_json_t* res = NULL;
    int           rc = skill_usecase_test_execute(pool, user_id, targ->skill_id, targ->args, &res);
    if (rc != 0 || !res) {
        if (res) {
            csilk_json_free(res);
        }
        snprintf(err_buf, err_cap, "测试执行失败");
        return -1;
    }

    if (out_result_json) {
        *out_result_json = csilk_json_serialize(res, NULL);
    }
    csilk_json_free(res);
    return 0;
}

void
api_ai_skills_test(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    int64_t     id = parse_query_int_strict(id_str, 0);
    if (id <= 0) {
        respond_error(c, 1002, "无效的技能 ID");
        return;
    }

    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t*    skill_obj = NULL;
    if (skill_usecase_get(pool, user_id, id, &skill_obj) != 0 || !skill_obj) {
        if (skill_obj) {
            csilk_json_free(skill_obj);
        }
        respond_error(c, 1002, "测试执行失败");
        return;
    }

    const char* exec_path = csilk_json_get_string(skill_obj, "exec_path");
    bool        is_l3 = (exec_path && exec_path[0] != '\0');
    csilk_json_free(skill_obj);

    csilk_json_t* body = csilk_bind_json(c);
    const char*   args = NULL;
    if (body) {
        args = body_get_str(body, "arguments");
    }

    /* L2 指令型技能无外部子进程，保持同步快速返回 */
    if (!is_l3) {
        csilk_json_t* res = NULL;
        int           rc = skill_usecase_test_execute(pool, user_id, id, args, &res);
        if (body) {
            csilk_json_free(body);
        }
        if (rc != 0 || !res) {
            respond_error(c, 1002, "测试执行失败");
            return;
        }
        respond_ok(c, res);
        return;
    }

    /* L3 可执行脚本技能走异步任务队列，避免阻塞 HTTP worker */
    skill_test_task_arg_t* targ = calloc(1, sizeof(*targ));
    if (!targ) {
        if (body) {
            csilk_json_free(body);
        }
        respond_error(c, 500, "内存不足");
        return;
    }
    targ->skill_id = id;
    if (args) {
        targ->args = strdup(args);
    }
    if (body) {
        csilk_json_free(body);
    }

    char dedupe_key[64];
    snprintf(dedupe_key, sizeof(dedupe_key), "skill:test:%lld", (long long)id);

    bool    deduplicated = false;
    int64_t task_id = mf_async_task_submit(pool,
                                           user_id,
                                           0,
                                           "skill_test",
                                           dedupe_key,
                                           "正在排队执行技能脚本测试",
                                           skill_test_task_fn,
                                           targ,
                                           skill_test_task_cleanup,
                                           &deduplicated);
    if (task_id <= 0) {
        respond_error(c, 1002, "提交异步技能测试任务失败");
        return;
    }

    mf_async_task_respond_submitted(c, task_id, "skill_test", deduplicated, NULL);
}

void
api_ai_skills_import(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_error(c, 1002, "请求体必须为合法 JSON");
        return;
    }

    const char* content = body_get_str(body, "content");
    if (!content || content[0] == '\0') {
        csilk_json_free(body);
        respond_error(c, 1002, "SKILL.md 内容不能为空");
        return;
    }

    int64_t new_id = 0;
    int     rc = skill_usecase_import_markdown(db_get_pool(), user_id, content, &new_id);
    csilk_json_free(body);

    if (rc == 1004) {
        respond_error(c, 1004, "导入失败：该技能名称已存在");
        return;
    } else if (rc != 0) {
        respond_error(c, 1002, "导入失败：YAML Frontmatter 格式或必填字段缺失");
        return;
    }

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_number(resp, "id", (double)new_id);
    respond_ok(c, resp);
}

void
api_ai_skills_export(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    int64_t     id = parse_query_int_strict(id_str, 0);
    if (id <= 0) {
        respond_error(c, 1002, "无效的技能 ID");
        return;
    }

    char* out_md = NULL;
    int   rc = skill_usecase_export_markdown(db_get_pool(), user_id, id, &out_md);
    if (rc != 0 || !out_md) {
        respond_error(c, 1002, "导出失败，技能不存在");
        return;
    }

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "content", out_md);
    free(out_md);
    respond_ok(c, resp);
}

void
api_ai_skills_reset_defaults(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    int rc = skill_usecase_reset_defaults(db_get_pool(), user_id);
    if (rc != 0) {
        respond_error(c, 1002, "重置预置技能失败");
        return;
    }
    respond_ok(c, NULL);
}

void
register_skill_routes(csilk_app_t* app)
{
    if (!app) {
        return;
    }
    csilk_app_get(app, "/api/ai/skills", api_ai_skills_list);
    csilk_app_get(app, "/api/ai/skills/:id", api_ai_skills_get);
    csilk_app_post(app, "/api/ai/skills", api_ai_skills_create);
    csilk_app_put(app, "/api/ai/skills/:id", api_ai_skills_update);
    csilk_app_delete(app, "/api/ai/skills/:id", api_ai_skills_delete);
    csilk_app_post(app, "/api/ai/skills/:id/toggle", api_ai_skills_toggle);
    csilk_app_post(app, "/api/ai/skills/:id/test", api_ai_skills_test);
    csilk_app_post(app, "/api/ai/skills/import", api_ai_skills_import);
    csilk_app_get(app, "/api/ai/skills/:id/export", api_ai_skills_export);
    csilk_app_post(app, "/api/ai/skills/reset-defaults", api_ai_skills_reset_defaults);
}
