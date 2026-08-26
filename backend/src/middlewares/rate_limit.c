#include "middlewares/rate_limit.h"
#include "common/response.h"
#include "csilk/core/response.h"
#include <string.h>
#include <time.h>

/* Per-endpoint sliding-window rate limiter (ring buffer, no mutex needed).
 *
 * Tracks (ip, path) hits and returns 429 when count >= MAX per WINDOW_SEC.
 * Capped at RING_SIZE entries to bound memory usage.
 */

#define RATE_LIMIT_WINDOW_SEC 60
#define RATE_LIMIT_MAX_REQS 10
#define RATE_LIMIT_RING 64

typedef struct {
    time_t ts;
    char   path[64];
    char   ip[64];
} entry_t;

static entry_t ring[RATE_LIMIT_RING];
static int     ring_head = 0;
static int     ring_count = 0;

static void
evict_old(time_t now)
{
    while (ring_count > 0 && (now - ring[ring_head].ts) > RATE_LIMIT_WINDOW_SEC) {
        ring_head = (ring_head + 1) % RATE_LIMIT_RING;
        ring_count--;
    }
}

void
rate_limit_auth_middleware(csilk_ctx_t* c)
{
    const char* path = csilk_get_path(c);
    if (!path) {
        csilk_next(c);
        return;
    }

    /* Only rate-limit auth-write endpoints */
    if (strcmp(path, "/api/auth/login") != 0 && strcmp(path, "/api/auth/register") != 0 &&
        strcmp(path, "/api/system/setup") != 0) {
        csilk_next(c);
        return;
    }

    const char* ip = csilk_get_client_ip(c);
    time_t      now = time(NULL);
    evict_old(now);

    int matches = 0;
    for (int i = 0; i < ring_count; i++) {
        int idx = (ring_head + i) % RATE_LIMIT_RING;
        if (ring[idx].ts == 0) {
            break;
        }
        if ((now - ring[idx].ts) <= RATE_LIMIT_WINDOW_SEC &&
            strncmp(ring[idx].path, path, sizeof(ring[idx].path) - 1) == 0 &&
            strncmp(ring[idx].ip, ip, sizeof(ring[idx].ip) - 1) == 0) {
            matches++;
        }
    }

    if (matches >= RATE_LIMIT_MAX_REQS) {
        csilk_json_t* resp = csilk_json_object();
        csilk_json_add_number(resp, "code", 1004);
        csilk_json_add_string(resp, "message", "请求过于频繁，请稍后再试");
        csilk_set_header(c, "Retry-After", "60");
        csilk_json(c, CSILK_STATUS_TOO_MANY_REQUESTS, resp);
        csilk_abort(c);
        return;
    }

    /* Record this request */
    if (ring_count >= RATE_LIMIT_RING) {
        ring_head = (ring_head + 1) % RATE_LIMIT_RING;
        ring_count--;
    }
    int idx = (ring_head + ring_count) % RATE_LIMIT_RING;
    ring[idx].ts = now;
    strncpy(ring[idx].path, path, sizeof(ring[idx].path) - 1);
    strncpy(ring[idx].ip, ip, sizeof(ring[idx].ip) - 1);
    ring_count++;

    csilk_next(c);
}
