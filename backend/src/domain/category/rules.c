#include "domain/category/rules.h"
#include <string.h>

bool
mf_category_rule_validate_name(const char* name)
{
    return name && name[0] && strlen(name) <= 128;
}

bool
mf_category_rule_validate_type(const char* type)
{
    if (!type || !type[0]) {
        return false;
    }
    return strcmp(type, "expense") == 0 || strcmp(type, "income") == 0 ||
           strcmp(type, "asset") == 0 || strcmp(type, "transaction") == 0;
}

bool
mf_category_rule_can_delete(int64_t child_count)
{
    return child_count == 0;
}
