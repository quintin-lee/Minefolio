#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "csilk/csilk.h"
#include "services/notification/wechat_push_service.h"

typedef struct {
    int         server_fd;
    int         port;
    char        last_received_body[2048];
    char        last_received_path[256];
    char        last_auth_header[256];
    const char* response_to_send;
    int         response_code;
    bool        stop;
} mock_server_t;

static void*
mock_server_thread(void* arg)
{
    mock_server_t* srv = (mock_server_t*)arg;

    while (!srv->stop) {
        struct sockaddr_in client_addr;
        socklen_t          addr_len = sizeof(client_addr);
        int                client_fd = accept(srv->server_fd, (struct sockaddr*)&client_addr, &addr_len);
        if (client_fd < 0) {
            if (srv->stop) {
                break;
            }
            continue;
        }

        char    buf[4096];
        ssize_t n = recv(client_fd, buf, sizeof(buf) - 1, 0);
        if (n > 0) {
            buf[n] = '\0';

            /* Parse path */
            char method[16], path[256];
            if (sscanf(buf, "%15s %255s", method, path) == 2) {
                snprintf(srv->last_received_path, sizeof(srv->last_received_path), "%s", path);
            }

            /* Parse auth header */
            char* auth_pos = strstr(buf, "Authorization: ");
            if (auth_pos) {
                char* auth_end = strstr(auth_pos, "\r\n");
                if (auth_end) {
                    size_t auth_len = auth_end - auth_pos;
                    if (auth_len < sizeof(srv->last_auth_header)) {
                        memcpy(srv->last_auth_header, auth_pos, auth_len);
                        srv->last_auth_header[auth_len] = '\0';
                    }
                }
            } else {
                srv->last_auth_header[0] = '\0';
            }

            /* Parse body (after \r\n\r\n) */
            char* body_pos = strstr(buf, "\r\n\r\n");
            if (body_pos) {
                snprintf(srv->last_received_body, sizeof(srv->last_received_body), "%s", body_pos + 4);
            } else {
                srv->last_received_body[0] = '\0';
            }

            /* Send HTTP Response */
            char resp[1024];
            int  code = srv->response_code ? srv->response_code : 200;
            const char* body = srv->response_to_send ? srv->response_to_send : "{\"code\":0}";
            snprintf(resp,
                     sizeof(resp),
                     "HTTP/1.1 %d OK\r\n"
                     "Content-Type: application/json\r\n"
                     "Content-Length: %zu\r\n"
                     "Connection: close\r\n\r\n"
                     "%s",
                     code,
                     strlen(body),
                     body);

            send(client_fd, resp, strlen(resp), 0);
        }
        close(client_fd);
    }

    return NULL;
}

static int
start_mock_server(mock_server_t* srv)
{
    memset(srv, 0, sizeof(*srv));
    srv->server_fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(srv->server_fd >= 0);

    int opt = 1;
    setsockopt(srv->server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sin.sin_port = 0;

    assert(bind(srv->server_fd, (struct sockaddr*)&sin, sizeof(sin)) == 0);

    socklen_t len = sizeof(sin);
    assert(getsockname(srv->server_fd, (struct sockaddr*)&sin, &len) == 0);
    srv->port = ntohs(sin.sin_port);

    assert(listen(srv->server_fd, 5) == 0);

    pthread_t th;
    pthread_create(&th, NULL, mock_server_thread, srv);
    pthread_detach(th);
    return 0;
}

static void
stop_mock_server(mock_server_t* srv)
{
    srv->stop = true;
    close(srv->server_fd);
    /* Wake up accept */
    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sin.sin_port = htons(srv->port);
    connect(s, (struct sockaddr*)&sin, sizeof(sin));
    close(s);
    usleep(20000);
}

int
main(void)
{
    printf("=== Starting WeChat Push Service Unit Tests ===\n");
    setenv("MINEFOLIO_ALLOW_LOCAL_WEBHOOK", "1", 1);
    mock_server_t server;
    start_mock_server(&server);

    char err_buf[256];

    /* 1. Test ServerChan Protocol */
    server.response_code = 200;
    server.response_to_send = "{\"code\":0,\"message\":\"success\"}";

    push_channel_config_t sc_cfg;
    memset(&sc_cfg, 0, sizeof(sc_cfg));
    snprintf(sc_cfg.channel_type, sizeof(sc_cfg.channel_type), "serverchan");
    snprintf(sc_cfg.secret_key, sizeof(sc_cfg.secret_key), "SCT12345");
    snprintf(sc_cfg.webhook_url, sizeof(sc_cfg.webhook_url), "http://127.0.0.1:%d/sct", server.port);

    int rc = wechat_push_service_send(&sc_cfg, "Daily Report", "# Market Summary\nAll good.", err_buf, sizeof(err_buf));
    assert(rc == 0);
    assert(strstr(server.last_received_path, "SCT12345.send") != NULL);
    assert(strstr(server.last_received_body, "Daily Report") != NULL);
    assert(strstr(server.last_received_body, "Market Summary") != NULL);

    /* 2. Test PushDeer Protocol */
    server.response_code = 200;
    server.response_to_send = "{\"code\":0,\"content\":{}}";

    push_channel_config_t pd_cfg;
    memset(&pd_cfg, 0, sizeof(pd_cfg));
    snprintf(pd_cfg.channel_type, sizeof(pd_cfg.channel_type), "pushdeer");
    snprintf(pd_cfg.secret_key, sizeof(pd_cfg.secret_key), "PDU998877");
    snprintf(pd_cfg.webhook_url, sizeof(pd_cfg.webhook_url), "http://127.0.0.1:%d", server.port);

    rc = wechat_push_service_send(&pd_cfg, "Alert: NVDA +5%", "Price triggered", err_buf, sizeof(err_buf));
    assert(rc == 0);
    assert(strstr(server.last_received_path, "/message/push") != NULL);
    assert(strstr(server.last_received_body, "PDU998877") != NULL);
    assert(strstr(server.last_received_body, "NVDA +5%") != NULL);
    assert(strstr(server.last_received_body, "markdown") != NULL);

    /* 3. Test Webhook Protocol with Bearer Token */
    server.response_code = 200;
    server.response_to_send = "{\"status\":\"ok\"}";

    push_channel_config_t wh_cfg;
    memset(&wh_cfg, 0, sizeof(wh_cfg));
    snprintf(wh_cfg.channel_type, sizeof(wh_cfg.channel_type), "webhook");
    snprintf(wh_cfg.secret_key, sizeof(wh_cfg.secret_key), "MY_TOKEN_ABC");
    snprintf(wh_cfg.webhook_url, sizeof(wh_cfg.webhook_url), "http://127.0.0.1:%d/custom/webhook", server.port);

    rc = wechat_push_service_send(&wh_cfg, "Webhook Title", "Payload content", err_buf, sizeof(err_buf));
    assert(rc == 0);
    assert(strcmp(server.last_received_path, "/custom/webhook") == 0);
    assert(strstr(server.last_auth_header, "Bearer MY_TOKEN_ABC") != NULL);
    assert(strstr(server.last_received_body, "Webhook Title") != NULL);

    /* 4. Test Server Error Handling (HTTP 500) */
    server.response_code = 500;
    server.response_to_send = "Internal Server Error";

    rc = wechat_push_service_send(&wh_cfg, "Will Fail", "test", err_buf, sizeof(err_buf));
    assert(rc == -1);
    assert(strstr(err_buf, "HTTP 500") != NULL);

    /* 5. Test ServerChan Business Error (code != 0) */
    server.response_code = 200;
    server.response_to_send = "{\"code\":40001,\"message\":\"bad sendkey\"}";

    rc = wechat_push_service_send(&sc_cfg, "Fail Report", "test", err_buf, sizeof(err_buf));
    assert(rc == -1);
    assert(strstr(err_buf, "ServerChan error code 40001") != NULL);
    assert(strstr(err_buf, "bad sendkey") != NULL);

    /* 6. Test Input Validation Failures */
    assert(wechat_push_service_send(NULL, "Title", "body", err_buf, sizeof(err_buf)) == -1);
    assert(wechat_push_service_send(&sc_cfg, "", "body", err_buf, sizeof(err_buf)) == -1);

    /* 7. Test SSRF protection when local webhooks disallowed */
    unsetenv("MINEFOLIO_ALLOW_LOCAL_WEBHOOK");
    rc = wechat_push_service_send(&wh_cfg, "SSRF Localhost", "test", err_buf, sizeof(err_buf));
    assert(rc == -1);
    assert(strstr(err_buf, "SSRF") != NULL);

    push_channel_config_t meta_cfg;
    memset(&meta_cfg, 0, sizeof(meta_cfg));
    snprintf(meta_cfg.channel_type, sizeof(meta_cfg.channel_type), "webhook");
    snprintf(meta_cfg.webhook_url, sizeof(meta_cfg.webhook_url), "http://169.254.169.254/latest");
    rc = wechat_push_service_send(&meta_cfg, "SSRF Metadata", "test", err_buf, sizeof(err_buf));
    assert(rc == -1);

    stop_mock_server(&server);
    printf("=== WeChat Push Service Unit Tests Passed! ===\n");
    return 0;
}
