#include "services/ai/trace/trace.h"
#include <stdlib.h>

ai_trace_t*
ai_trace_create(int64_t user_id, int64_t session_id)
{
    ai_trace_t* t = (ai_trace_t*)calloc(1, sizeof(ai_trace_t));
    if (!t) {
        return NULL;
    }
    ai_trace_init(t, user_id, session_id);
    return t;
}

void
ai_trace_destroy(ai_trace_t* trace)
{
    if (!trace) {
        return;
    }
    ai_trace_free(trace);
    free(trace);
}
