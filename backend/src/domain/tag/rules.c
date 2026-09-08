#include "domain/tag/rules.h"
#include <string.h>

bool
mf_tag_rule_validate_name(const char* name)
{
    if (!name || !name[0]) {
        return false;
    }
    return strlen(name) <= 128;
}

bool
mf_tag_rule_validate_color(const char* color)
{
    if (!color || !color[0]) {
        return true; /* optional — caller will use default */
    }
    /* Accept #RRGGBB format (7 chars) or any non-empty string for flexibility */
    return color[0] == '#' && strlen(color) == 7;
}
