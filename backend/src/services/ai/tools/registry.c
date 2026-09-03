#include "services/ai/tools/registry.h"
#include "services/ai/tools/asset_tool.h"
#include "services/ai/tools/transaction_tool.h"
#include "services/ai/tools/expense_tool.h"
#include "services/ai/tools/transfer_tool.h"
#include "services/ai/tools/cashflow_tool.h"
#include "services/ai/tools/portfolio_tool.h"
#include "services/ai/tools/report_tool.h"
#include <stdlib.h>
#include <string.h>

#define MAX_TOOLS 64

static ai_tool_t       s_registered_tools[MAX_TOOLS];
static size_t          s_tool_count = 0;
static bool            s_initialized = false;
static csilk_ai_tool_t s_csilk_tools[MAX_TOOLS];
static size_t          s_csilk_tool_count = 0;

void
ai_tool_registry_init(void)
{
    if (s_initialized) {
        return;
    }
    s_tool_count = 0;
    s_csilk_tool_count = 0;

    ai_tool_asset_register_all();
    ai_tool_transaction_register_all();
    ai_tool_expense_register_all();
    ai_tool_transfer_register_all();
    ai_tool_cashflow_register_all();
    ai_tool_portfolio_register_all();
    ai_tool_report_register_all();

    s_initialized = true;
}

int
ai_tool_register(const ai_tool_t* tool)
{
    if (!tool || !tool->name || !tool->name[0] || s_tool_count >= MAX_TOOLS) {
        return -1;
    }
    for (size_t i = 0; i < s_tool_count; i++) {
        if (strcmp(s_registered_tools[i].name, tool->name) == 0) {
            s_registered_tools[i] = *tool;
            return 0;
        }
    }
    s_registered_tools[s_tool_count++] = *tool;
    return 0;
}

int
ai_tool_unregister(const char* name)
{
    if (!name || !name[0]) {
        return -1;
    }
    for (size_t i = 0; i < s_tool_count; i++) {
        if (strcmp(s_registered_tools[i].name, name) == 0) {
            for (size_t j = i; j + 1 < s_tool_count; j++) {
                s_registered_tools[j] = s_registered_tools[j + 1];
            }
            s_tool_count--;
            return 0;
        }
    }
    return -1;
}

const ai_tool_t*
ai_tool_find(const char* name)
{
    if (!name || !name[0]) {
        return NULL;
    }
    ai_tool_registry_init();
    for (size_t i = 0; i < s_tool_count; i++) {
        if (strcmp(s_registered_tools[i].name, name) == 0) {
            return &s_registered_tools[i];
        }
    }
    return NULL;
}

const ai_tool_t**
ai_tool_list_all(size_t* count)
{
    ai_tool_registry_init();
    static const ai_tool_t* list[MAX_TOOLS];
    for (size_t i = 0; i < s_tool_count; i++) {
        list[i] = &s_registered_tools[i];
    }
    if (count) {
        *count = s_tool_count;
    }
    return list;
}

const csilk_ai_tool_t*
ai_tool_get_csilk_definitions(size_t* count)
{
    ai_tool_registry_init();
    s_csilk_tool_count = 0;
    for (size_t i = 0; i < s_tool_count; i++) {
        s_csilk_tools[i].type = "function";
        s_csilk_tools[i].function.name = s_registered_tools[i].name;
        s_csilk_tools[i].function.description = s_registered_tools[i].description;
        s_csilk_tools[i].function.parameters_json = s_registered_tools[i].parameters_schema;
        s_csilk_tool_count++;
    }
    if (count) {
        *count = s_csilk_tool_count;
    }
    return s_csilk_tools;
}
