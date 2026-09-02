#include "services/ai/model/request.h"
#include "services/ai_tools.h"
#include <stdlib.h>

csilk_json_t*
ai_model_build_request_json(const ai_model_request_params_t* params)
{
    if (!params) {
        return NULL;
    }
    csilk_json_t* req = csilk_json_object();
    csilk_json_add_string(req, "model", params->model ? params->model : "gpt-4o");
    if (params->messages) {
        csilk_json_add_object(req, "messages", csilk_json_copy(params->messages));
    } else {
        csilk_json_add_array(req, "messages", csilk_json_array());
    }
    csilk_json_add_bool(req, "stream", params->stream);
    csilk_json_add_number(req, "temperature", params->temperature > 0 ? params->temperature : 0.7);

    if (params->enable_tools) {
        size_t                 tool_count = 0;
        const csilk_ai_tool_t* tools = ai_tools_get_definitions(&tool_count);
        if (tools && tool_count > 0) {
            csilk_json_t* tools_arr = csilk_json_array();
            for (size_t i = 0; i < tool_count; i++) {
                csilk_json_t* t = csilk_json_object();
                csilk_json_add_string(t, "type", "function");
                csilk_json_t* fn = csilk_json_object();
                csilk_json_add_string(fn, "name", tools[i].function.name);
                csilk_json_add_string(fn, "description", tools[i].function.description);
                if (tools[i].function.parameters_json) {
                    csilk_json_add_object(
                        fn,
                        "parameters",
                        csilk_json_copy((csilk_json_t*)tools[i].function.parameters_json));
                }
                csilk_json_add_object(t, "function", fn);
                csilk_json_array_append(tools_arr, t);
            }
            csilk_json_add_array(req, "tools", tools_arr);
            csilk_json_add_string(req, "tool_choice", "auto");
        }
    }

    return req;
}
