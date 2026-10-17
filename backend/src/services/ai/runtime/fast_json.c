#include "services/ai/runtime/fast_json.h"
#include <stdint.h>
#include <stdio.h>

int
ai_json_escape_fast(const char* src, size_t src_len, char* dst, size_t dst_cap)
{
    if (!src || !dst || dst_cap == 0) {
        return -1;
    }

    static const char hex_digits[] = "0123456789abcdef";
    size_t d = 0;

    for (size_t s = 0; s < src_len; s++) {
        unsigned char c = (unsigned char)src[s];

        if (c == '"') {
            if (d + 2 >= dst_cap) return -1;
            dst[d++] = '\\';
            dst[d++] = '"';
        } else if (c == '\\') {
            if (d + 2 >= dst_cap) return -1;
            dst[d++] = '\\';
            dst[d++] = '\\';
        } else if (c == '\b') {
            if (d + 2 >= dst_cap) return -1;
            dst[d++] = '\\';
            dst[d++] = 'b';
        } else if (c == '\f') {
            if (d + 2 >= dst_cap) return -1;
            dst[d++] = '\\';
            dst[d++] = 'f';
        } else if (c == '\n') {
            if (d + 2 >= dst_cap) return -1;
            dst[d++] = '\\';
            dst[d++] = 'n';
        } else if (c == '\r') {
            if (d + 2 >= dst_cap) return -1;
            dst[d++] = '\\';
            dst[d++] = 'r';
        } else if (c == '\t') {
            if (d + 2 >= dst_cap) return -1;
            dst[d++] = '\\';
            dst[d++] = 't';
        } else if (c < 0x20) {
            /* 控制字符转义为 \u00XX */
            if (d + 6 >= dst_cap) return -1;
            dst[d++] = '\\';
            dst[d++] = 'u';
            dst[d++] = '0';
            dst[d++] = '0';
            dst[d++] = hex_digits[(c >> 4) & 0x0F];
            dst[d++] = hex_digits[c & 0x0F];
        } else {
            /* 普通 ASCII 字符及 UTF-8 高位直接透明拷贝 */
            if (d + 1 >= dst_cap) return -1;
            dst[d++] = (char)c;
        }
    }

    dst[d] = '\0';
    return (int)d;
}
