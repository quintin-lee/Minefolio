#include "services/ai/trace/span.h"
#include <string.h>
#include <stdlib.h>

void
ai_span_start(ai_span_t* span, const char* name, ai_span_type_t type)
{
    if (!span) {
        return;
    }
    memset(span, 0, sizeof(ai_span_t));
    if (name) {
        strncpy(span->name, name, sizeof(span->name) - 1);
    }
    span->type = type;
    clock_gettime(CLOCK_MONOTONIC, &span->start_time);
    strncpy(span->status, "running", sizeof(span->status) - 1);
}

void
ai_span_finish(ai_span_t* span, const char* status, const char* error)
{
    if (!span) {
        return;
    }
    clock_gettime(CLOCK_MONOTONIC, &span->end_time);
    span->duration_ms = (span->end_time.tv_sec - span->start_time.tv_sec) * 1000 +
                        (span->end_time.tv_nsec - span->start_time.tv_nsec) / 1000000;
    if (span->duration_ms < 0) {
        span->duration_ms = 0;
    }
    strncpy(span->status, status ? status : "ok", sizeof(span->status) - 1);
    if (error) {
        span->error = strdup(error);
    }
}
