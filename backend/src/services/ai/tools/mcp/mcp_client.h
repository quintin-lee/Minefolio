#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "csilk/protocols/mcp.h"
#include "domain/mcp/entity.h"

/**
 * @file mcp_client.h
 * @brief MCP 远端工具源客户端 (MCP Client) — M2
 *
 * 通过 JSON-RPC 2.0 帧与远端 MCP 服务器通信（streamable-HTTP 或 stdio）。
 * 帧收发复用 csilk_mcp_msg_parse / csilk_mcp_msg_serialize。
 *
 * 传输层：
 *  - HTTP：libcurl 短连接（每次调用独立），请求体 JSON，响应头 Mcp-Session-Id 不持久化
  *  - STDIO：fork+execve 拉起子进程，stdin 写、stdout 读，换行分帧；空闲超时清理
 */

/** MCP 客户端句柄（每用户每服务器一个，短生命周期，函数内建即拆） */
typedef struct {
    const mf_mcp_server_t* server;
    char*                  session_id; /* HTTP：Mcp-Session-Id；stdio 为 NULL */
    int                    read_fd;    /* stdio：子进程 stdout 读端 */
    int                    write_fd;   /* stdio：子进程 stdin 写端 */
    pid_t                  child_pid;  /* stdio 子进程；0 = http */
    char                   last_error[256];
} mf_mcp_client_t;

/**
 * @brief 向远端 MCP 服务器发送 initialize 握手。
 * @return 0 成功, -1 失败（失败原因写入 out_err）
 */
int mf_mcp_client_initialize(mf_mcp_client_t* c, char* out_err, size_t err_sz);

/**
 * @brief 调用远端 tools/list，返回工具 schema 缓存实体数组（调用方负责释放）。
 * @param out_tools  [out] 堆分配的 mf_mcp_server_tool_t* 数组
 * @param out_count  [out] 工具数量
 * @return 0 成功, -1 失败
 */
int mf_mcp_client_list_tools(mf_mcp_client_t*       c,
                             int64_t                server_id,
                             int64_t                user_id,
                             mf_mcp_server_tool_t** out_tools,
                             size_t*                out_count);

/**
 * @brief 调用远端 tools/call，返回结果（堆分配 JSON 字符串，调用方 free）。
 * @param tool_name  远端原始工具名（不带前缀）
 * @param args       工具参数 JSON 字符串
 * @return 结果 JSON 字符串（堆分配，调用方 free），或 NULL（错误写入 out_err）
 */
char* mf_mcp_client_call_tool(
    mf_mcp_client_t* c, const char* tool_name, const char* args_json, char* out_err, size_t err_sz);

/* ---- 句柄生命周期 ---- */

/**
 * @brief 创建一个 MCP 客户端句柄（依据 server 配置；stdio 暂不 spawn，延迟到首帧）
 */
mf_mcp_client_t* mf_mcp_client_new(const mf_mcp_server_t* server);

/**
 * @brief 释放客户端句柄（stdio 子进程 SIGTERM→SIGKILL 双段清理）
 */
void mf_mcp_client_free(mf_mcp_client_t* c);
