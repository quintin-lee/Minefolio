/**
 * @file ai_session_repo.c
 * @brief AI 会话与消息数据访问层具体实现
 *
 * 实现了 AI 会话与聊天消息的 SQL 数据交互逻辑，包括分页计算、参数字符串格式化、
 * 动态 SQL 构建以及数据记录的提取与内存释放。
 */

#include "repositories/ai_session_repo.h"
#include "common/db.h"
#include <stdio.h>
#include <string.h>

/**
 * @brief 将 64 位整数格式化为定长字符串缓冲区（会话 ID 专用辅助函数）
 *
 * @param sid 64 位会话 ID
 * @param out 输出字符串缓冲区（至少 32 字节容量）
 */
static void
sid_str(int64_t sid, char out[static 32])
{
    snprintf(out, 32, "%lld", (long long)sid);
}

/**
 * @brief 将 64 位整数格式化为定长字符串缓冲区（用户 ID 专用辅助函数）
 *
 * @param uid 64 位用户 ID
 * @param out 输出字符串缓冲区（至少 32 字节容量）
 */
static void
uid_str(int64_t uid, char out[static 32])
{
    snprintf(out, 32, "%lld", (long long)uid);
}

/**
 * @brief 分页查询用户的 AI 会话列表
 *
 * 查询流程：
 * 1. 绑定 `user_id` 参数查询 `COUNT(*)` 得到该用户的会话总数。
 * 2. 计算分页偏移量 `offset = (page - 1) * page_size`。
 * 3. 构造参数化查询：`SELECT ... FROM ai_sessions WHERE user_id=? ORDER BY updated_at DESC LIMIT ? OFFSET ?`。
 * 4. 释放计数结果 JSON 对象，返回会话列表 JSON 数组。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param page 当前页码 (从 1 开始)
 * @param page_size 每页显示条数
 * @param[out] total 输出参数，返回匹配的总记录数
 * @return csilk_json_t* 包含会话对象的 JSON 数组
 */
csilk_json_t*
ai_session_list(
    csilk_db_pool_t* pool, int64_t user_id, int64_t page, int64_t page_size, int64_t* total)
{
    char uid[32], lim[32], off[32];
    uid_str(user_id, uid);
    snprintf(lim, 32, "%lld", (long long)page_size);
    snprintf(off, 32, "%lld", (long long)((page - 1) * page_size));

    /* 1. 查询总记录数 */
    csilk_json_t* cnt =
        csilk_db_query_param_json(pool,
                                  "SELECT COUNT(*) as cnt FROM ai_sessions WHERE user_id=?",
                                  (const char*[]){uid, NULL});
    *total = 0;
    if (cnt && csilk_json_array_size(cnt) > 0) {
        *total = db_get_int(csilk_json_array_get(cnt, 0), "cnt");
    }
    csilk_json_free(cnt);

    /* 2. 分页查询会话记录，按更新时间倒序排列 */
    return csilk_db_query_param_json(
        pool,
        "SELECT id, user_id, title, model, provider, "
        "created_at, updated_at "
        "FROM ai_sessions WHERE user_id=? ORDER BY updated_at DESC LIMIT ? OFFSET ?",
        (const char*[]){uid, lim, off, NULL});
}

/**
 * @brief 根据会话 ID 查询用户的单个 AI 会话记录
 *
 * 执行参数化 SQL：`SELECT ... FROM ai_sessions WHERE id=? AND user_id=?`。
 * 同时校验 `id` 与 `user_id` 确保数据租户隔离。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 会话 ID
 * @return csilk_json_t* 包含会话详情的 JSON 数组（长度为 1）；若未命中则自动释放并返回 NULL
 */
csilk_json_t*
ai_session_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id)
{
    char uid[32], id_s[32];
    uid_str(user_id, uid);
    sid_str(id, id_s);
    csilk_json_t* r = csilk_db_query_param_json(pool,
                                                "SELECT id, user_id, title, model, provider, "
                                                "created_at, updated_at "
                                                "FROM ai_sessions WHERE id=? AND user_id=?",
                                                (const char*[]){id_s, uid, NULL});
    if (!r || csilk_json_array_size(r) == 0) {
        csilk_json_free(r);
        return NULL;
    }
    return r;
}

/**
 * @brief 插入新的 AI 会话记录并获取其自增 ID
 *
 * 执行 SQL：`INSERT INTO ai_sessions (user_id, title, model, provider) VALUES (?, ?, ?, ?) RETURNING id`。
 * 当 model 或 provider 为 NULL 时提供缺省兜底值。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param title 会话标题（为 NULL 时使用默认值 "新对话"）
 * @param model 模型名称（为 NULL 时使用默认值 "deepseek-chat"）
 * @param provider 提供商名称（为 NULL 时使用默认值 "deepseek"）
 * @return int64_t 成功生成的主键 ID，失败返回 0
 */
int64_t
ai_session_insert(csilk_db_pool_t* pool,
                  int64_t          user_id,
                  const char*      title,
                  const char*      model,
                  const char*      provider)
{
    char uid[32], m[128], p[64];
    uid_str(user_id, uid);
    strncpy(m, model ?: "deepseek-chat", sizeof(m) - 1);
    m[sizeof(m) - 1] = '\0';
    strncpy(p, provider ?: "deepseek", sizeof(p) - 1);
    p[sizeof(p) - 1] = '\0';
    csilk_json_t* r =
        csilk_db_query_param_json(pool,
                                  "INSERT INTO ai_sessions (user_id, title, model, provider) "
                                  "VALUES (?, ?, ?, ?) RETURNING id",
                                  (const char*[]){uid, title ?: "新对话", m, p, NULL});
    int64_t id = 0;
    if (r && csilk_json_array_size(r) > 0) {
        id = db_get_int(csilk_json_array_get(r, 0), "id");
    }
    csilk_json_free(r);
    return id;
}

/**
 * @brief 动态更新 AI 会话标题与模型名称
 *
 * 根据传入的 title 和 model 是否有效，动态拼接 SET 字段与参数列表，
 * 同时包含 `updated_at=CURRENT_TIMESTAMP`。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 会话 ID
 * @param title 新标题（若为空或 NULL 则不更新）
 * @param model 新模型名称（若为空或 NULL 则不更新）
 * @return int 成功更新返回 1，若无更新字段或更新失败返回 0
 */
int
ai_session_update(
    csilk_db_pool_t* pool, int64_t user_id, int64_t id, const char* title, const char* model)
{
    char uid[32], id_s[32];
    uid_str(user_id, uid);
    sid_str(id, id_s);
    if ((!title || !title[0]) && (!model || !model[0])) {
        return 1;
    }

    char sql[1024];
    int  idx = snprintf(sql, sizeof(sql), "UPDATE ai_sessions SET updated_at=CURRENT_TIMESTAMP");
    int  pc = 0;
    const char* params[8];
    if (title && title[0]) {
        idx += snprintf(sql + idx, (size_t)(sizeof(sql) - (size_t)idx), ", title=?");
        params[pc++] = title;
    }
    if (model && model[0]) {
        idx += snprintf(sql + idx, (size_t)(sizeof(sql) - (size_t)idx), ", model=?");
        params[pc++] = model;
    }
    idx += snprintf(
        sql + idx, (size_t)(sizeof(sql) - (size_t)idx), " WHERE id=? AND user_id=? RETURNING id");
    params[pc++] = id_s;
    params[pc++] = uid;
    params[pc] = NULL;

    csilk_json_t* r = csilk_db_query_param_json(pool, sql, params);
    int           affected = r ? (int)csilk_json_array_size(r) : 0;
    csilk_json_free(r);
    return affected > 0;
}

/**
 * @brief 删除指定的 AI 会话记录
 *
 * 执行 SQL：`DELETE FROM ai_sessions WHERE id=? AND user_id=? RETURNING id`。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 会话 ID
 * @return int 成功删除返回 1，否则返回 0
 */
int
ai_session_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id)
{
    char uid[32], id_s[32];
    uid_str(user_id, uid);
    sid_str(id, id_s);
    csilk_json_t* r =
        csilk_db_query_param_json(pool,
                                  "DELETE FROM ai_sessions WHERE id=? AND user_id=? RETURNING id",
                                  (const char*[]){id_s, uid, NULL});
    int affected = r ? (int)csilk_json_array_size(r) : 0;
    csilk_json_free(r);
    return affected > 0;
}

/**
 * @brief 删除指定会话中最后一条助手的回复消息
 *
 * 采用子查询定位目标 ID：
 * `DELETE FROM ai_messages WHERE id = (SELECT id FROM ai_messages WHERE session_id=? AND role='assistant' ORDER BY id DESC LIMIT 1) RETURNING id`
 *
 * @param pool 数据库连接池指针
 * @param session_id 会话 ID
 * @return int 成功删除的记录数量
 */
int
ai_message_delete_last_assistant(csilk_db_pool_t* pool, int64_t session_id)
{
    char sid[32];
    sid_str(session_id, sid);
    csilk_json_t* r = csilk_db_query_param_json(
        pool,
        "DELETE FROM ai_messages WHERE id = ("
        "  SELECT id FROM ai_messages WHERE session_id=? AND role='assistant' "
        "  ORDER BY id DESC LIMIT 1"
        ") RETURNING id",
        (const char*[]){sid, NULL});
    int affected = r ? (int)csilk_json_array_size(r) : 0;
    if (r) {
        csilk_json_free(r);
    }
    return affected;
}

/**
 * @brief 分页查询指定会话的消息历史
 *
 * 执行两步查询：
 * 1. `SELECT COUNT(*) as cnt FROM ai_messages WHERE session_id=?` 获取消息总量。
 * 2. `SELECT id, session_id, role, content, model, created_at FROM ai_messages WHERE session_id=? ORDER BY created_at ASC LIMIT ? OFFSET ?` 获取当前页。
 *
 * @param pool 数据库连接池指针
 * @param session_id 会话 ID
 * @param page 当前页码 (从 1 开始)
 * @param page_size 每页数量
 * @param[out] total 输出参数，返回消息总数
 * @return csilk_json_t* 包含消息对象的 JSON 数组
 */
csilk_json_t*
ai_message_list(
    csilk_db_pool_t* pool, int64_t session_id, int64_t page, int64_t page_size, int64_t* total)
{
    char sid[32], lim[32], off[32];
    sid_str(session_id, sid);
    snprintf(lim, 32, "%lld", (long long)page_size);
    snprintf(off, 32, "%lld", (long long)((page - 1) * page_size));

    /* 1. 统计消息总量 */
    csilk_json_t* cnt =
        csilk_db_query_param_json(pool,
                                  "SELECT COUNT(*) as cnt FROM ai_messages WHERE session_id=?",
                                  (const char*[]){sid, NULL});
    *total = 0;
    if (cnt && csilk_json_array_size(cnt) > 0) {
        *total = db_get_int(csilk_json_array_get(cnt, 0), "cnt");
    }
    csilk_json_free(cnt);

    /* 2. 按时间升序分页获取消息 */
    return csilk_db_query_param_json(
        pool,
        "SELECT id, session_id, role, content, model, created_at "
        "FROM ai_messages WHERE session_id=? ORDER BY created_at ASC LIMIT ? OFFSET ?",
        (const char*[]){sid, lim, off, NULL});
}

/**
 * @brief 获取会话最近的 N 条消息上下文（按时间正序输出）
 *
 * 先通过子查询获取最近逆序排列的 `limit` 条记录，外层再按 `created_at ASC, id ASC` 重新排序：
 * `SELECT role, content FROM (SELECT role, content, created_at, id FROM ai_messages WHERE session_id=? ORDER BY created_at DESC, id DESC LIMIT ?) t ORDER BY created_at ASC, id ASC`
 *
 * @param pool 数据库连接池指针
 * @param session_id 会话 ID
 * @param limit 获取的最大消息数量
 * @return csilk_json_t* 包含 role, content 的 JSON 数组
 */
csilk_json_t*
ai_message_recent(csilk_db_pool_t* pool, int64_t session_id, int limit)
{
    char sid[32], lim[32];
    sid_str(session_id, sid);
    snprintf(lim, 32, "%d", limit);
    return csilk_db_query_param_json(pool,
                                     "SELECT role, content FROM ("
                                     "  SELECT role, content, created_at, id "
                                     "  FROM ai_messages WHERE session_id=? "
                                     "  ORDER BY created_at DESC, id DESC "
                                     "  LIMIT ?"
                                     ") t ORDER BY created_at ASC, id ASC",
                                     (const char*[]){sid, lim, NULL});
}

/**
 * @brief 插入一条新的会话消息
 *
 * 执行 SQL：`INSERT INTO ai_messages (session_id, role, content, model) VALUES (?, ?, ?, ?) RETURNING id`。
 *
 * @param pool 数据库连接池指针
 * @param session_id 目标会话 ID
 * @param role 发送者角色 (user, assistant, system 等)
 * @param content 消息正文
 * @param model 所使用的模型名称（可选）
 * @return int64_t 新增消息的主键 ID，失败返回 0
 */
int64_t
ai_message_insert(csilk_db_pool_t* pool,
                  int64_t          session_id,
                  const char*      role,
                  const char*      content,
                  const char*      model)
{
    char sid[32], m[128];
    sid_str(session_id, sid);
    strncpy(m, model ?: "", sizeof(m) - 1);
    m[sizeof(m) - 1] = '\0';
    csilk_json_t* r = csilk_db_query_param_json(pool,
                                                "INSERT INTO ai_messages (session_id, role, "
                                                "content, model) VALUES (?, ?, ?, ?) RETURNING id",
                                                (const char*[]){sid, role, content, m, NULL});
    int64_t       id = 0;
    if (r && csilk_json_array_size(r) > 0) {
        id = db_get_int(csilk_json_array_get(r, 0), "id");
    }
    csilk_json_free(r);
    return id;
}
