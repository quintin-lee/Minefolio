#include "services/ai/policy/confirmation.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>

#define DRAFT_SECRET "minefolio_ai_draft_v1"
#define DRAFT_TTL_SEC 300

static void
draft_hmac(const char* data, char* out_hex, size_t outlen)
{
    unsigned char md[SHA256_DIGEST_LENGTH];
    unsigned int  mdlen = 0;
    HMAC(EVP_sha256(),
         DRAFT_SECRET,
         (int)strlen(DRAFT_SECRET),
         (const unsigned char*)data,
         strlen(data),
         md,
         &mdlen);
    static const char* hex = "0123456789abcdef";
    size_t             i = 0;
    for (; i < mdlen && i * 2 + 1 < outlen; i++) {
        out_hex[i * 2] = hex[md[i] >> 4];
        out_hex[i * 2 + 1] = hex[md[i] & 0xf];
    }
    out_hex[i * 2] = '\0';
}

char*
ai_confirmation_create_token(int64_t user_id, double amount, const char* a, const char* b)
{
    time_t exp = time(NULL) + DRAFT_TTL_SEC;
    char   canon[512];
    snprintf(canon,
             sizeof(canon),
             "%lld|%.2f|%s|%s|%lld",
             (long long)user_id,
             amount,
             a ? a : "",
             b ? b : "",
             (long long)exp);
    char mac[SHA256_DIGEST_LENGTH * 2 + 1];
    draft_hmac(canon, mac, sizeof(mac));
    char* tok = malloc(strlen(mac) + 32);
    if (!tok) {
        return NULL;
    }
    snprintf(tok, strlen(mac) + 32, "%s.%lld", mac, (long long)exp);
    return tok;
}

int
ai_confirmation_verify_token(
    int64_t user_id, double amount, const char* a, const char* b, const char* token)
{
    if (!token || !token[0]) {
        return 0;
    }
    const char* dot = strrchr(token, '.');
    if (!dot) {
        return 0;
    }
    long long exp = atoll(dot + 1);
    time_t    now = time(NULL);
    if (exp <= 0 || (time_t)exp < now) {
        return 0; /* Expired */
    }
    char canon[512];
    snprintf(canon,
             sizeof(canon),
             "%lld|%.2f|%s|%s|%lld",
             (long long)user_id,
             amount,
             a ? a : "",
             b ? b : "",
             exp);
    char expected_mac[SHA256_DIGEST_LENGTH * 2 + 1];
    draft_hmac(canon, expected_mac, sizeof(expected_mac));
    size_t mac_len = (size_t)(dot - token);
    if (mac_len != strlen(expected_mac)) {
        return 0;
    }
    if (strncmp(token, expected_mac, mac_len) != 0) {
        return 0;
    }
    return 1;
}

ai_confirmation_draft_t*
ai_confirmation_draft_create(const char* action_name,
                             const char* summary,
                             const char* payload_json,
                             const char* warning)
{
    ai_confirmation_draft_t* draft =
        (ai_confirmation_draft_t*)calloc(1, sizeof(ai_confirmation_draft_t));
    if (!draft) {
        return NULL;
    }

    snprintf(draft->draft_id, sizeof(draft->draft_id), "draft_%ld", (long)time(NULL));

    if (action_name) {
        strncpy(draft->action_name, action_name, sizeof(draft->action_name) - 1);
    }
    if (summary) {
        draft->summary = strdup(summary);
    }
    if (payload_json) {
        draft->payload_json = strdup(payload_json);
    }
    if (warning) {
        draft->warning_message = strdup(warning);
    }

    draft->requires_confirmation = true;
    return draft;
}

void
ai_confirmation_draft_free(ai_confirmation_draft_t* draft)
{
    if (!draft) {
        return;
    }
    if (draft->summary) {
        free(draft->summary);
    }
    if (draft->payload_json) {
        free(draft->payload_json);
    }
    if (draft->warning_message) {
        free((void*)draft->warning_message);
    }
    free(draft);
}

char*
ai_confirmation_draft_to_json(const ai_confirmation_draft_t* draft)
{
    if (!draft) {
        return strdup("{}");
    }

    csilk_json_t* obj = csilk_json_object();
    csilk_json_add_bool(obj, "requires_confirmation", draft->requires_confirmation);
    csilk_json_add_string(obj, "draft_id", draft->draft_id);
    csilk_json_add_string(obj, "action_name", draft->action_name);
    csilk_json_add_string(obj, "summary", draft->summary ? draft->summary : "高风险操作确认");
    csilk_json_add_string(obj, "warning", draft->warning_message ? draft->warning_message : "");

    if (draft->payload_json && draft->payload_json[0]) {
        csilk_json_t* payload = csilk_json_parse(draft->payload_json);
        if (payload) {
            csilk_json_add_object(obj, "payload", payload);
        }
    }

    size_t len = 0;
    char*  out = csilk_json_serialize(obj, &len);
    csilk_json_free(obj);
    return out ? out : strdup("{}");
}
