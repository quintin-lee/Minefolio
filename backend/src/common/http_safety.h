#pragma once

/**
 * @file http_safety.h
 * @brief 出站 HTTP 请求统一 SSRF 防护、DNS 固定解析 (CURLOPT_RESOLVE) 与响应体上限封装
 *
 * 消除多处 libcurl 调用点各自实现或缺失 SSRF 防护的问题：
 *  - 统一校验证书、协议（仅 http/https）、端口与目标主机/IP（拦截回环、私网、CGNAT、
 *    链路本地 169.254/16、多播与 IPv4-mapped/ULA IPv6）；
 *  - 将 getaddrinfo 解析出的首个合法公网 IP 通过 CURLOPT_RESOLVE 绑定到 CURL 句柄，
 *    从根源阻断验证后到建连之间的 DNS Rebinding TOCTOU 攻击；
 *  - 提供带严格最大字节上限的有界响应写入回调，防止恶意上游响应导致 OOM。
 */

#include <curl/curl.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 默认最大响应体上限定义 */
#define MF_CURL_MAX_1MB (1U * 1024U * 1024U)
#define MF_CURL_MAX_4MB (4U * 1024U * 1024U)
#define MF_CURL_MAX_8MB (8U * 1024U * 1024U)

/** 有界 libcurl 响应缓冲区 */
typedef struct {
    char*  data;
    size_t size;
    size_t cap;
    size_t max_bytes;
    bool   overflow;
} mf_curl_buf_t;

/**
 * @brief 判断主机名或 IP 字符串是否属于内网/回环/保留地址（不做 DNS 查询）
 */
bool mf_http_host_is_private_literal(const char* host);

/**
 * @brief 校验出站 URL 安全性，并生成用于 CURLOPT_RESOLVE 的 "host:port:ip" 绑定字符串
 *
 * @param[in] url 目标 HTTP/HTTPS URL
 * @param[in] max_len 允许最大 URL 长度（0 表示默认 2048）
 * @param[in] allow_local 是否允许连接 localhost / RFC1918 私网（例如显式配置的本地 Ollama /
 *                        测试 mock）；即便 allow_local=true 仍阻断云元数据网段 169.254.0.0/16。
 * @param[in] has_proxy 若请求通过显式 HTTP/SOCKS 代理出站，DNS 解析失败时仅执行字面量校验，
 *                      跳过本地 getaddrinfo 阻断，且不注入 CURLOPT_RESOLVE。
 * @param[out] resolve_entry 若非 NULL，写入格式为 "host:port:resolved_ip"（用于 CURLOPT_RESOLVE；
 *                           当 URL 本身已是字面量 IP 或使用代理时为空字符串）
 * @param[in] resolve_cap resolve_entry 缓冲区大小（建议 >= 320）
 * @return int 0 校验通过；-1 非法 URL 或 SSRF 拦截
 */
int mf_http_validate_and_resolve_url(const char* url,
                                     size_t      max_len,
                                     bool        allow_local,
                                     bool        has_proxy,
                                     char*       resolve_entry,
                                     size_t      resolve_cap);

/**
 * @brief 为 CURL 句柄施加统一安全选项与 DNS Pinning
 *
 * 设置 CURLOPT_NOSIGNAL=1、限制协议为 HTTP/HTTPS、默认禁用盲目 3xx 自动跳转，
 * 开启严格 TLS 证书/主机校验，并在未使用 HTTP 代理时设置 CURLOPT_RESOLVE。
 *
 * @param[in,out] curl libcurl 句柄
 * @param[in] resolve_entry mf_http_validate_and_resolve_url 输出的绑定串（可为 NULL）
 * @param[in] proxy 若本次请求走了代理，则跳过 CURLOPT_RESOLVE（由代理侧解析）
 * @return struct curl_slist* 返回需在请求结束后 curl_slist_free_all() 释放的 resolve 链表，或 NULL
 */
struct curl_slist* mf_http_curl_apply_security(CURL*       curl,
                                               const char* resolve_entry,
                                               const char* proxy,
                                               bool        allow_redirects);

/**
 * @brief 带严格最大长度上限的 libcurl 写入回调（userdata 必须为 mf_curl_buf_t*）
 */
size_t mf_curl_bounded_write_cb(void* contents, size_t size, size_t nmemb, void* userdata);

#ifdef __cplusplus
}
#endif
