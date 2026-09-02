#include "services/ai/tools/schema.h"

void
ai_schema_add_prop(csilk_json_t* props, const char* name, const char* type, const char* desc)
{
    if (!props || !name) {
        return;
    }
    csilk_json_t* p = csilk_json_object();
    csilk_json_add_string(p, "type", type ? type : "string");
    if (desc) {
        csilk_json_add_string(p, "description", desc);
    }
    csilk_json_add_object(props, name, p);
}

csilk_json_t*
ai_schema_create(csilk_json_t* props, const char** required_names, int req_count)
{
    csilk_json_t* s = csilk_json_object();
    csilk_json_add_string(s, "type", "object");
    csilk_json_add_object(s, "properties", props ? props : csilk_json_object());
    if (required_names && req_count > 0) {
        csilk_json_t* req = csilk_json_array();
        for (int i = 0; i < req_count; i++) {
            csilk_json_array_append(req, csilk_json_string_new(required_names[i]));
        }
        csilk_json_add_array(s, "required", req);
    }
    return s;
}
