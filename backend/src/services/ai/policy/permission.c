#include "services/ai/policy/permission.h"
#include "common/db.h"
#include "infrastructure/repositories/auth_repo_impl.h"
#include "csilk/core/server.h"
#include <pthread.h>
#include <string.h>

#define MAX_ROLE_OVERRIDES 32

typedef struct {
    int64_t        user_id;
    ai_user_role_t role;
} user_role_entry_t;

static user_role_entry_t s_role_overrides[MAX_ROLE_OVERRIDES];
static size_t            s_role_count = 0;

ai_permission_level_t
ai_permission_get_level(const char* tool_name)
{
    if (!tool_name) {
        return AI_PERM_READ;
    }
    if (strcmp(tool_name, "delete_asset") == 0 || strcmp(tool_name, "delete_transaction") == 0 ||
        strcmp(tool_name, "reset_account") == 0 ||
        strcmp(tool_name, "confirm_proposed_transfer") == 0 ||
        strcmp(tool_name, "execute_transfer") == 0) {
        return AI_PERM_SENSITIVE;
    }
    if (strncmp(tool_name, "create_", 7) == 0 || strncmp(tool_name, "update_", 7) == 0 ||
        strncmp(tool_name, "add_", 4) == 0 || strncmp(tool_name, "set_", 4) == 0 ||
        strncmp(tool_name, "propose_", 8) == 0 || strncmp(tool_name, "confirm_", 8) == 0) {
        return AI_PERM_WRITE;
    }
    return AI_PERM_READ;
}

bool
ai_permission_role_allows(ai_user_role_t user_role, ai_permission_level_t required_level)
{
    switch (user_role) {
    case AI_USER_ROLE_GUEST:
        return false;
    case AI_USER_ROLE_VIEWER:
        return required_level == AI_PERM_READ;
    case AI_USER_ROLE_STANDARD:
        return required_level == AI_PERM_READ || required_level == AI_PERM_WRITE;
    case AI_USER_ROLE_ADMIN:
        return true;
    default:
        return false;
    }
}

ai_user_role_t
ai_permission_get_user_role(int64_t user_id)
{
    if (user_id <= 0) {
        return AI_USER_ROLE_GUEST;
    }
    for (size_t i = 0; i < s_role_count; i++) {
        if (s_role_overrides[i].user_id == user_id) {
            return s_role_overrides[i].role;
        }
    }
    /* 实例管理员（/system/setup 首个用户）持 ADMIN 权限；查询结果进程内缓存
     * （admin id 生命周期内稳定，MIN(id) 查询只在首次触发） */
    static pthread_mutex_t admin_lock = PTHREAD_MUTEX_INITIALIZER;
    static int64_t         admin_id = 0;
    static bool            admin_resolved = false;
    pthread_mutex_lock(&admin_lock);
    if (!admin_resolved) {
        admin_id = (int64_t)mf_auth_repo_is_instance_admin(db_get_pool(), user_id) > 0
                       ? user_id
                       : -1; /* -1 = 非管理员；避免每次调用都打 DB */
        admin_resolved = true;
    }
    bool is_admin = (admin_id > 0 && user_id == admin_id);
    pthread_mutex_unlock(&admin_lock);
    if (is_admin) {
        return AI_USER_ROLE_ADMIN;
    }
    /* 普通已认证用户：STANDARD（READ+WRITE）；SENSITIVE 工具仍需管理员 */
    return AI_USER_ROLE_STANDARD;
}

void
ai_permission_set_user_role(int64_t user_id, ai_user_role_t role)
{
    for (size_t i = 0; i < s_role_count; i++) {
        if (s_role_overrides[i].user_id == user_id) {
            s_role_overrides[i].role = role;
            return;
        }
    }
    if (s_role_count < MAX_ROLE_OVERRIDES) {
        s_role_overrides[s_role_count].user_id = user_id;
        s_role_overrides[s_role_count].role = role;
        s_role_count++;
    }
}

bool
ai_permission_check(int64_t user_id, ai_permission_level_t required_level)
{
    ai_user_role_t role = ai_permission_get_user_role(user_id);
    return ai_permission_role_allows(role, required_level);
}
