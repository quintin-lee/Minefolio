#pragma once
#include <stddef.h>
#include "domain/skill/entity.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Validates skill name against Anthropic Agent Skills kebab-case standard:
 *        1-64 chars, lowercase alphanumeric and single hyphens, no leading/trailing hyphens.
 * @return 0 on valid, -1 on invalid.
 */
int mf_skill_validate_name(const char* name);

/**
 * @brief Validates relative execution script path:
 *        Must not be absolute, must not contain '..' segments, must not contain newlines.
 *        Empty string is valid (meaning prompt-only skill).
 * @return 0 on valid, -1 on invalid.
 */
int mf_skill_validate_exec_path(const char* path);

/**
 * @brief Validates the entire skill entity before persistence.
 * @param skill The entity to validate.
 * @param err_buf Buffer to write error message into on failure.
 * @param err_size Size of err_buf.
 * @return 0 on valid, -1 on invalid.
 */
int mf_skill_validate(const mf_skill_t* skill, char* err_buf, size_t err_size);

#ifdef __cplusplus
}
#endif
