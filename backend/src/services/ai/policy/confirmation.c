#include "services/ai/policy/confirmation.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
