#include "common/sse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
mf_sse_init(csilk_ctx_t* c)
{
    if (!c) {
        return -1;
    }

    csilk_status(c, CSILK_STATUS_OK);
    csilk_set_header(c, "Content-Type", "text/event-stream");
    csilk_set_header(c, "Cache-Control", "no-cache");
    csilk_set_header(c, "X-Accel-Buffering", "no");

    /* 强制按短连接收尾：确保同步处理器末尾调用 csilk_response_end() 输出 "0\r\n\r\n" 后
       立即切入 CSILK_CONN_CLOSING，既避免 _csilk_dispatch_request 二次发包，也防止客户端
       复用 keep-alive 连接池命中已停读的句柄。 */
    csilk_set_request_header(c, "Connection", "close");

    return csilk_response_write(c, (const uint8_t*)"", 0);
}

int
mf_sse_send(csilk_ctx_t* c, const char* event, const char* data)
{
    if (!c || csilk_ctx_is_closed(c)) {
        return -1;
    }

    if (!event && !data) {
        static const char keepalive[] = ": keepalive\n\n";
        return csilk_response_write(c, (const uint8_t*)keepalive, sizeof(keepalive) - 1);
    }

    size_t ev_len = event ? strlen(event) : 0;
    size_t data_len = data ? strlen(data) : 0;
    size_t need = ev_len + data_len + 32;

    char  stack_buf[4096];
    char* buf = stack_buf;
    if (need > sizeof(stack_buf)) {
        buf = (char*)malloc(need);
        if (!buf) {
            return -1;
        }
    }

    int pos = 0;
    if (event) {
        pos += snprintf(buf + pos, need - (size_t)pos, "event: %s\n", event);
    }
    if (data) {
        pos += snprintf(buf + pos, need - (size_t)pos, "data: %s\n", data);
    }
    pos += snprintf(buf + pos, need - (size_t)pos, "\n");

    int rc = csilk_response_write(c, (const uint8_t*)buf, (size_t)pos);
    if (buf != stack_buf) {
        free(buf);
    }
    return rc;
}

void
mf_sse_end(csilk_ctx_t* c)
{
    if (!c) {
        return;
    }
    csilk_response_end(c);
}
