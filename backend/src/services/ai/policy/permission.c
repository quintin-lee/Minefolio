#include "services/ai/policy/permission.h"
#include <string.h>

ai_permission_level_t
ai_permission_get_level(const char* tool_name)
{
    if (!tool_name) {
        return AI_PERM_READ;
    }
    if (strcmp(tool_name, "delete_asset") == 0 || strcmp(tool_name, "delete_transaction") == 0 ||
        strcmp(tool_name, "reset_account") == 0) {
        return AI_PERM_SENSITIVE;
    }
    if (strncmp(tool_name, "create_", 7) == 0 || strncmp(tool_name, "update_", 7) == 0 ||
        strncmp(tool_name, "add_", 4) == 0 || strncmp(tool_name, "set_", 4) == 0) {
        return AI_PERM_WRITE;
    }
    return AI_PERM_READ;
}

bool
ai_permission_check(int64_t user_id, ai_permission_level_t required_level)
{
    (void)required_level;
    return user_id > 0;
}
