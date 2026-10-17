#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 将原始文本按照 RFC 8259 规范就地转义并输出到目标缓冲区
 *
 * @param src 原始字符串起始指针
 * @param src_len 原始字符串长度
 * @param dst 目标缓冲区
 * @param dst_cap 目标缓冲区最大容量 (必须保留至少 1 字节存储 '\0')
 * @return int 成功写入目标缓冲区的字符数 (不含末尾 '\0')；若缓冲区不足则返回 -1
 */
int ai_json_escape_fast(const char* src, size_t src_len, char* dst, size_t dst_cap);

#ifdef __cplusplus
}
#endif
