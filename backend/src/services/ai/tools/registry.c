#include "services/ai/tools/registry.h"
#include "services/ai_tools.h"
#include <string.h>

#define MAX_TOOLS 64
static ai_tool_def_t g_registry[MAX_TOOLS];
static size_t        g_registry_count = 0;

void
ai_tool_registry_init(void)
{
    g_registry_count = 0;
}

int
ai_tool_registry_register(const ai_tool_def_t* tool)
{
    if (!tool || !tool->name || g_registry_count >= MAX_TOOLS) {
        return -1;
    }
    for (size_t i = 0; i < g_registry_count; i++) {
        if (strcmp(g_registry[i].name, tool->name) == 0) {
            g_registry[i] = *tool;
            return 0;
        }
    }
    g_registry[g_registry_count++] = *tool;
    return 0;
}

const ai_tool_def_t*
ai_tool_registry_find(const char* name)
{
    if (!name) {
        return NULL;
    }
    for (size_t i = 0; i < g_registry_count; i++) {
        if (strcmp(g_registry[i].name, name) == 0) {
            return &g_registry[i];
        }
    }
    return NULL;
}

const csilk_ai_tool_t*
ai_tool_registry_get_csilk_tools(size_t* count)
{
    return ai_tools_get_definitions(count);
}
