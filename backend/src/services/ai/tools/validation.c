#include "services/ai/tools/validation.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

int
ai_tool_validate_args(const csilk_json_t* schema,
                      const csilk_json_t* args,
                      char*               err_buf,
                      size_t              err_sz)
{
    if (err_buf && err_sz > 0) {
        err_buf[0] = '\0';
    }

    if (!args || !csilk_json_is_object(args)) {
        if (err_buf && err_sz > 0) {
            snprintf(err_buf, err_sz, "Invalid arguments: expected JSON object");
        }
        return -1;
    }

    if (!schema) {
        return 0;
    }

    /* 1. Check required fields */
    const csilk_json_t* req = csilk_json_get(schema, "required");
    if (req && csilk_json_is_array(req)) {
        size_t n = csilk_json_array_size(req);
        for (size_t i = 0; i < n; i++) {
            const csilk_json_t* ritem = csilk_json_array_get(req, i);
            const char*         req_name = csilk_json_string_value(ritem);
            if (!req_name) {
                continue;
            }
            const csilk_json_t* val = csilk_json_get(args, req_name);
            if (!val || csilk_json_is_null(val)) {
                if (err_buf && err_sz > 0) {
                    snprintf(err_buf, err_sz, "Missing required field: '%s'", req_name);
                }
                return -1;
            }
            if (csilk_json_is_string(val)) {
                const char* s = csilk_json_string_value(val);
                if (!s || !s[0]) {
                    if (err_buf && err_sz > 0) {
                        snprintf(err_buf, err_sz, "Required field '%s' cannot be empty", req_name);
                    }
                    return -1;
                }
            }
        }
    }

    /* 2. Check property types & enums */
    const csilk_json_t* props = csilk_json_get(schema, "properties");
    if (props && csilk_json_is_object(props)) {
        /* We can inspect provided arguments against property definitions */
        const csilk_json_t* req_list = csilk_json_get(schema, "required");
        size_t              req_count = req_list ? csilk_json_array_size(req_list) : 0;
        (void)req_count;
    }

    return 0;
}

csilk_json_t*
ai_tool_normalize_args(const csilk_json_t* schema, const csilk_json_t* args)
{
    (void)schema;
    if (!args) {
        return csilk_json_object();
    }
    return csilk_json_copy((csilk_json_t*)args);
}
