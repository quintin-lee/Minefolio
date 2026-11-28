#include "services/ai/tools/registry.h"
#include "services/ai/tools/asset_tool.h"
#include "services/ai/tools/transaction_tool.h"
#include "services/ai/tools/expense_tool.h"
#include "services/ai/tools/transfer_tool.h"
#include "services/ai/tools/cashflow_tool.h"
#include "services/ai/tools/portfolio_tool.h"
#include "services/ai/tools/report_tool.h"
#include "services/ai/tools/news_tool.h"
#include "services/ai/tools/web_search_tool.h"
#include "services/ai/tools/batch_tool.h"
#include "services/ai/tools/skill_tool.h"
#include "services/ai/tools/metadata_tool.h"
#include "services/ai/tools/budget_tool.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TOOLS AI_TOOL_MAX_TOOLS

static ai_tool_t        s_registered_tools[MAX_TOOLS];
static size_t           s_tool_count = 0;
static bool             s_initialized = false;
static const ai_tool_t* s_tool_list_cache[MAX_TOOLS];
static csilk_ai_tool_t  s_csilk_tools[MAX_TOOLS];
static size_t           s_csilk_tool_count = 0;
static csilk_ai_tool_t  s_csilk_readonly_tools[MAX_TOOLS];
static size_t           s_csilk_readonly_count = 0;

/* 注册表与派生视图缓存统一受 s_registry_mutex 保护；初始化/注册时一次性重建只读快照，
   读取路径只读返回快照指针，避免多线程并发调用时清零覆写静态数组引发数据竞争。 */
static pthread_once_t  s_registry_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t s_registry_mutex = PTHREAD_MUTEX_INITIALIZER;

static void
rebuild_caches_locked(void)
{
    s_csilk_tool_count = 0;
    s_csilk_readonly_count = 0;
    for (size_t i = 0; i < s_tool_count; i++) {
        s_tool_list_cache[i] = &s_registered_tools[i];
        if (s_registered_tools[i].internal_only) {
            continue; /* 内部专用工具（如 confirm_*）绝不暴露给 LLM，防止 Prompt Injection 自主绕过确认 */
        }
        s_csilk_tools[s_csilk_tool_count].type = "function";
        s_csilk_tools[s_csilk_tool_count].function.name = s_registered_tools[i].name;
        s_csilk_tools[s_csilk_tool_count].function.description = s_registered_tools[i].description;
        s_csilk_tools[s_csilk_tool_count].function.parameters_json =
            s_registered_tools[i].parameters_schema;
        s_csilk_tool_count++;

        if (!s_registered_tools[i].is_mutation) {
            s_csilk_readonly_tools[s_csilk_readonly_count].type = "function";
            s_csilk_readonly_tools[s_csilk_readonly_count].function.name =
                s_registered_tools[i].name;
            s_csilk_readonly_tools[s_csilk_readonly_count].function.description =
                s_registered_tools[i].description;
            s_csilk_readonly_tools[s_csilk_readonly_count].function.parameters_json =
                s_registered_tools[i].parameters_schema;
            s_csilk_readonly_count++;
        }
    }
}

static void
registry_init_impl(void)
{
    pthread_mutex_lock(&s_registry_mutex);
    s_tool_count = 0;
    s_csilk_tool_count = 0;
    s_csilk_readonly_count = 0;
    pthread_mutex_unlock(&s_registry_mutex);

    ai_tool_asset_register_all();
    ai_tool_transaction_register_all();
    ai_tool_expense_register_all();
    ai_tool_transfer_register_all();
    ai_tool_cashflow_register_all();
    ai_tool_portfolio_register_all();
    ai_tool_report_register_all();
    ai_tool_news_register_all();
    ai_tool_web_search_register_all();
    ai_tool_batch_register_all();
    ai_tool_skill_register_all();
    ai_tool_metadata_register_all();
    ai_tool_budget_register_all();

    pthread_mutex_lock(&s_registry_mutex);
    rebuild_caches_locked();
    s_initialized = true;
    pthread_mutex_unlock(&s_registry_mutex);
}

void
ai_tool_registry_init(void)
{
    pthread_once(&s_registry_once, registry_init_impl);
}

bool
ai_tool_requires_id(const char* name)
{
    if (!name || !name[0]) {
        return false;
    }
    pthread_once(&s_registry_once, registry_init_impl);
    pthread_mutex_lock(&s_registry_mutex);
    bool required = false;
    for (size_t i = 0; i < s_tool_count; i++) {
        if (strcmp(s_registered_tools[i].name, name) == 0) {
            required = s_registered_tools[i].requires_id;
            break;
        }
    }
    pthread_mutex_unlock(&s_registry_mutex);
    return required;
}

int
ai_tool_register(const ai_tool_t* tool)
{
    if (!tool || !tool->name || !tool->name[0]) {
        return -1;
    }
    pthread_mutex_lock(&s_registry_mutex);
    for (size_t i = 0; i < s_tool_count; i++) {
        if (strcmp(s_registered_tools[i].name, tool->name) == 0) {
            s_registered_tools[i] = *tool;
            rebuild_caches_locked();
            pthread_mutex_unlock(&s_registry_mutex);
            return 0;
        }
    }
    if (s_tool_count >= MAX_TOOLS) {
        pthread_mutex_unlock(&s_registry_mutex);
        return -1;
    }
    s_registered_tools[s_tool_count++] = *tool;
    rebuild_caches_locked();
    pthread_mutex_unlock(&s_registry_mutex);
    return 0;
}

int
ai_tool_unregister(const char* name)
{
    if (!name || !name[0]) {
        return -1;
    }
    pthread_mutex_lock(&s_registry_mutex);
    for (size_t i = 0; i < s_tool_count; i++) {
        if (strcmp(s_registered_tools[i].name, name) == 0) {
            for (size_t j = i; j + 1 < s_tool_count; j++) {
                s_registered_tools[j] = s_registered_tools[j + 1];
            }
            s_tool_count--;
            rebuild_caches_locked();
            pthread_mutex_unlock(&s_registry_mutex);
            return 0;
        }
    }
    pthread_mutex_unlock(&s_registry_mutex);
    return -1;
}

const ai_tool_t*
ai_tool_find(const char* name)
{
    if (!name || !name[0]) {
        return NULL;
    }
    ai_tool_registry_init();
    pthread_mutex_lock(&s_registry_mutex);
    const ai_tool_t* found = NULL;
    for (size_t i = 0; i < s_tool_count; i++) {
        if (strcmp(s_registered_tools[i].name, name) == 0) {
            found = &s_registered_tools[i];
            break;
        }
    }
    pthread_mutex_unlock(&s_registry_mutex);
    return found;
}

const char*
ai_tool_find_confirm_tool_by_action(const char* action_type)
{
    if (!action_type || !action_type[0]) {
        return NULL;
    }
    ai_tool_registry_init();
    pthread_mutex_lock(&s_registry_mutex);
    const char* name = NULL;
    for (size_t i = 0; i < s_tool_count; i++) {
        const ai_tool_t* t = &s_registered_tools[i];
        if (t->internal_only && t->action_type && strcmp(t->action_type, action_type) == 0) {
            name = t->name;
            break;
        }
    }
    pthread_mutex_unlock(&s_registry_mutex);
    return name;
}

const ai_tool_t**
ai_tool_list_all(size_t* count)
{
    ai_tool_registry_init();
    pthread_mutex_lock(&s_registry_mutex);
    if (count) {
        *count = s_tool_count;
    }
    pthread_mutex_unlock(&s_registry_mutex);
    return s_tool_list_cache;
}

const csilk_ai_tool_t*
ai_tool_get_csilk_definitions(size_t* count)
{
    ai_tool_registry_init();
    pthread_mutex_lock(&s_registry_mutex);
    if (count) {
        *count = s_csilk_tool_count;
    }
    pthread_mutex_unlock(&s_registry_mutex);
    return s_csilk_tools;
}

const csilk_ai_tool_t*
ai_tool_get_readonly_csilk_definitions(size_t* count)
{
    ai_tool_registry_init();
    pthread_mutex_lock(&s_registry_mutex);
    if (count) {
        *count = s_csilk_readonly_count;
    }
    pthread_mutex_unlock(&s_registry_mutex);
    return s_csilk_readonly_tools;
}

size_t
ai_tool_get_csilk_definitions_subset(const char* const* names,
                                     size_t             count,
                                     csilk_ai_tool_t*   out,
                                     size_t             out_cap)
{
    if (!names || !out || out_cap == 0) {
        return 0;
    }
    ai_tool_registry_init();
    size_t written = 0;
    pthread_mutex_lock(&s_registry_mutex);
    for (size_t ni = 0; ni < count && written < out_cap; ni++) {
        const char* name = names[ni];
        if (!name || !name[0]) {
            continue;
        }
        for (size_t i = 0; i < s_tool_count; i++) {
            const ai_tool_t* t = &s_registered_tools[i];
            if (t->internal_only || strcmp(t->name, name) != 0) {
                continue;
            }
            out[written].type = "function";
            out[written].function.name = t->name;
            out[written].function.description = t->description;
            out[written].function.parameters_json = t->parameters_schema;
            written++;
            break;
        }
    }
    pthread_mutex_unlock(&s_registry_mutex);
    return written;
}
