#include "services/ai/tools/tool_spec.h"
#include "services/ai/tools/schema.h"

csilk_json_t*
ai_tool_spec_build_schema(const ai_tool_spec_t* spec)
{
    if (!spec) {
        return ai_schema_create_object();
    }
    if (spec->raw_schema_json && spec->raw_schema_json[0]) {
        csilk_json_t* parsed = csilk_json_parse(spec->raw_schema_json);
        if (parsed) {
            return parsed;
        }
    }

    csilk_json_t* s = ai_schema_create_object();
    size_t        n = spec->param_count;
    if (n > AI_TOOL_MAX_PARAMS) {
        n = AI_TOOL_MAX_PARAMS;
    }

    for (size_t i = 0; i < n; i++) {
        const ai_tool_param_spec_t* p = &spec->params[i];
        if (!p->name || !p->name[0]) {
            continue;
        }
        if (p->enum_values[0] != NULL) {
            size_t enum_cnt = 0;
            while (enum_cnt < AI_TOOL_MAX_ENUM_VALUES && p->enum_values[enum_cnt] != NULL) {
                enum_cnt++;
            }
            ai_schema_add_prop_enum(s, p->name, p->description, p->enum_values, enum_cnt);
        } else {
            ai_schema_add_prop(s, p->name, p->type ? p->type : "string", p->description);
        }
        if (p->required) {
            ai_schema_add_required(s, p->name);
        }
    }
    return s;
}

int
ai_tool_register_spec(const ai_tool_spec_t* spec)
{
    if (!spec || !spec->name || !spec->name[0]) {
        return -1;
    }
    csilk_json_t* schema = ai_tool_spec_build_schema(spec);
    ai_tool_t     t = {
            .name = spec->name,
            .description = spec->description,
            .action_type = spec->action_type,
            .parameters_schema = schema,
            .permission = spec->permission,
            .risk = spec->risk,
            .is_mutation = spec->is_mutation,
            .requires_id = spec->requires_id,
            .internal_only = spec->internal_only,
            .validate = spec->validate,
            .execute = spec->execute,
    };
    return ai_tool_register(&t);
}

int
ai_tool_register_specs(const ai_tool_spec_t* specs, size_t count)
{
    if (!specs) {
        return -1;
    }
    for (size_t i = 0; i < count; i++) {
        if (ai_tool_register_spec(&specs[i]) != 0) {
            return -1;
        }
    }
    return 0;
}
