#include "services/ai/runtime/error.h"
#include <stdio.h>
#include <string.h>

const char*
ai_runtime_error_name(ai_runtime_error_t code)
{
    switch (code) {
    case AI_RUNTIME_ERR_OK:
        return "OK";
    case AI_RUNTIME_ERR_MODEL:
        return "MODEL_ERROR";
    case AI_RUNTIME_ERR_TOOL:
        return "TOOL_ERROR";
    case AI_RUNTIME_ERR_POLICY:
        return "POLICY_ERROR";
    case AI_RUNTIME_ERR_TIMEOUT:
        return "TIMEOUT";
    case AI_RUNTIME_ERR_CONTEXT_OVERFLOW:
        return "CONTEXT_OVERFLOW";
    case AI_RUNTIME_ERR_VALIDATION:
        return "VALIDATION_ERROR";
    case AI_RUNTIME_ERR_CANCELLED:
        return "CANCELLED";
    default:
        return "UNKNOWN_ERROR";
    }
}

void
ai_runtime_status_set(ai_runtime_status_t* status,
                      ai_runtime_error_t   code,
                      const char*          message,
                      const char*          detail)
{
    if (!status) {
        return;
    }
    status->code = code;
    if (message) {
        strncpy(status->message, message, sizeof(status->message) - 1);
        status->message[sizeof(status->message) - 1] = '\0';
    } else {
        status->message[0] = '\0';
    }
    if (detail) {
        strncpy(status->detail, detail, sizeof(status->detail) - 1);
        status->detail[sizeof(status->detail) - 1] = '\0';
    } else {
        status->detail[0] = '\0';
    }
}
