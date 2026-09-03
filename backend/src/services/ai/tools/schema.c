#include "services/ai/tools/schema.h"
#include <stdlib.h>
#include <string.h>

csilk_json_t*
ai_schema_create_object(void)
{
    csilk_json_t* s = csilk_json_object();
    csilk_json_add_string(s, "type", "object");
    csilk_json_add_object(s, "properties", csilk_json_object());
    csilk_json_add_array(s, "required", csilk_json_array());
    return s;
}

static csilk_json_t*
get_or_create_props(csilk_json_t* schema)
{
    csilk_json_t* props = csilk_json_get(schema, "properties");
    if (!props) {
        props = csilk_json_object();
        csilk_json_add_object(schema, "properties", props);
    }
    return props;
}

void
ai_schema_add_prop(csilk_json_t* schema,
                   const char*   name,
                   const char*   type,
                   const char*   description)
{
    if (!schema || !name || !type) {
        return;
    }
    csilk_json_t* props = get_or_create_props(schema);
    csilk_json_t* p = csilk_json_object();
    csilk_json_add_string(p, "type", type);
    if (description) {
        csilk_json_add_string(p, "description", description);
    }
    csilk_json_add_object(props, name, p);
}

void
ai_schema_add_prop_enum(csilk_json_t* schema,
                        const char*   name,
                        const char*   description,
                        const char**  enum_values,
                        size_t        enum_count)
{
    if (!schema || !name || !enum_values) {
        return;
    }
    csilk_json_t* props = get_or_create_props(schema);
    csilk_json_t* p = csilk_json_object();
    csilk_json_add_string(p, "type", "string");
    if (description) {
        csilk_json_add_string(p, "description", description);
    }
    csilk_json_t* en_arr = csilk_json_array();
    for (size_t i = 0; i < enum_count; i++) {
        if (enum_values[i]) {
            csilk_json_array_append(en_arr, csilk_json_string_new(enum_values[i]));
        }
    }
    csilk_json_add_array(p, "enum", en_arr);
    csilk_json_add_object(props, name, p);
}

void
ai_schema_add_required(csilk_json_t* schema, const char* name)
{
    if (!schema || !name) {
        return;
    }
    csilk_json_t* req = csilk_json_get(schema, "required");
    if (!req) {
        req = csilk_json_array();
        csilk_json_add_array(schema, "required", req);
    }
    csilk_json_array_append(req, csilk_json_string_new(name));
}
