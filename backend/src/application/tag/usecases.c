#include "application/tag/usecases.h"
#include "domain/tag/rules.h"
#include "infrastructure/repositories/tag_repo_impl.h"
#include "common/db.h"
#include <stdio.h>
#include <string.h>

int
tag_usecase_list(void* pool, int64_t user_id, csilk_json_t** out_list)
{
    mf_tag_t* tags = NULL;
    size_t    count = 0;
    if (mf_tag_repo_list(pool, user_id, &tags, &count) != 0) {
        return -1;
    }
    csilk_json_t* arr = csilk_json_array();
    for (size_t i = 0; i < count; i++) {
        csilk_json_t* obj = csilk_json_object();
        csilk_json_add_number(obj, "id", (double)tags[i].id);
        csilk_json_add_string(obj, "name", tags[i].name);
        csilk_json_add_string(obj, "color", tags[i].color);
        csilk_json_add_string(obj, "created_at", tags[i].created_at);
        csilk_json_add_item(arr, obj);
    }
    mf_tag_repo_free_list(tags, count);
    *out_list = arr;
    return 0;
}

int
tag_usecase_suggestions(void* pool, int64_t user_id, const char* prefix, csilk_json_t** out_list)
{
    mf_tag_t* tags = NULL;
    size_t    count = 0;
    if (mf_tag_repo_suggestions(pool, user_id, prefix, &tags, &count) != 0) {
        return -1;
    }
    csilk_json_t* arr = csilk_json_array();
    for (size_t i = 0; i < count; i++) {
        csilk_json_t* obj = csilk_json_object();
        csilk_json_add_number(obj, "id", (double)tags[i].id);
        csilk_json_add_string(obj, "name", tags[i].name);
        csilk_json_add_string(obj, "color", tags[i].color);
        csilk_json_add_item(arr, obj);
    }
    mf_tag_repo_free_list(tags, count);
    *out_list = arr;
    return 0;
}

int
tag_usecase_create(void*                   pool,
                   const create_tag_cmd_t* cmd,
                   int64_t*                out_id,
                   tag_usecase_result_t*   out_res)
{
    if (!mf_tag_rule_validate_name(cmd->name)) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "标签名称不能为空");
        return -1;
    }
    if (!mf_tag_rule_validate_color(cmd->color)) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "颜色格式无效，应为 #RRGGBB");
        return -1;
    }
    return mf_tag_repo_create(pool, cmd->user_id, cmd->name, cmd->color, out_id);
}

int
tag_usecase_update(void* pool, const update_tag_cmd_t* cmd, tag_usecase_result_t* out_res)
{
    if (cmd->name && !mf_tag_rule_validate_name(cmd->name)) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "标签名称不能为空");
        return -1;
    }
    if (cmd->color && !mf_tag_rule_validate_color(cmd->color)) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "颜色格式无效，应为 #RRGGBB");
        return -1;
    }
    int rc = mf_tag_repo_update(pool, cmd->user_id, cmd->tag_id, cmd->name, cmd->color);
    if (rc == 1) {
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "标签不存在");
    }
    return rc == 1 ? 0 : rc;
}

int
tag_usecase_delete(void* pool, const delete_tag_cmd_t* cmd, tag_usecase_result_t* out_res)
{
    int rc = mf_tag_repo_delete(pool, cmd->user_id, cmd->tag_id);
    if (rc == 1) {
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "标签不存在");
    }
    return rc == 1 ? 0 : rc;
}
