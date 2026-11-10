#pragma once

#include "common/ai_config.h"
#include "csilk/csilk.h"
#include "csilk/drivers/ai.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 AI 配置提供者（应用启动时调用一次）
 * @param pool 数据库连接池指针（可为 NULL，NULL 时回退读取本地配置文件）
 */
void ai_config_provider_init(void* pool);

/**
 * @brief 关闭 AI 配置提供者并摘除写者持有的快照引用
 */
void ai_config_provider_shutdown(void);

/**
 * @brief 热重载 AI 配置（settings_update 成功后由用例层主动调用）
 *        内部原子替换快照，在进行中的请求继续安全持有旧快照
 * @param pool 数据库连接池指针
 * @return 0 成功，非 0 失败
 */
int ai_config_provider_reload(void* pool);

/**
 * @brief 读者获取当前配置快照引用（引用计数原子 +1）
 *        调用方在用完后必须显式调用 ai_config_release()！
 * @return ai_config_t* 配置快照指针，未就绪时返回 NULL
 */
ai_config_t* ai_config_acquire(void);

/**
 * @brief 读者释放持有的配置快照引用（引用计数原子 -1，归零时物理销毁快照）
 * @param cfg 由 ai_config_acquire() 获取的配置指针
 */
void ai_config_release(ai_config_t* cfg);

/**
 * @brief 获取快照内预建的 csilk_ai_t 实例（须在持有 cfg 保护下调用）
 * @param cfg 由 ai_config_acquire() 获取的有效快照指针
 * @return csilk_ai_t* 驱动实例指针或 NULL
 */
csilk_ai_t* ai_config_get_ai_instance(const ai_config_t* cfg);

/**
 * @brief RAII 风格作用域守卫宏（退出代码块时自动执行 release）
 */
#define AI_CONFIG_SCOPE(cfg)                                                                       \
    for (ai_config_t* cfg = ai_config_acquire(), *__once = (void*)1; __once && cfg;                \
         __once = NULL, ai_config_release(cfg))

#ifdef __cplusplus
}
#endif
