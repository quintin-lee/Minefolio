#pragma once

/**
 * @file dtos.h
 * @brief MCP 服务器用例结果 DTO (MCP Application DTOs)
 */

#include <stdint.h>

/** MCP 服务器用例通用结果 */
typedef struct {
    int  code;         /**< 错误码：0=成功, 1002=校验失败, 1003=不存在 */
    char message[256]; /**< 错误描述 */
} mf_mcp_usecase_result_t;
