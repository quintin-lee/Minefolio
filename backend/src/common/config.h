#pragma once
#include <stddef.h>

/**
 * @brief Read a string value from a JSON config file.
 * @param path  Path to the JSON config file (e.g. "config/db.json").
 * @param key   Key to look up.
 * @param out   Output buffer (must be at least out_size bytes).
 * @param out_size  Size of out buffer.
 * @return 0 on success, -1 if file not found / key missing / read error.
 */
int config_get_str(const char* path, const char* key, char* out, size_t out_size);

/**
 * @brief Write a JSON config file with the given key-value pairs.
 *        Existing keys not mentioned are preserved; new keys are added.
 * @param path  Output path (e.g. "config/db.json").
 * @param kv    NULL-terminated flat array of key, value, key, value, ...
 * @return 0 on success, -1 on error.
 */
int config_set(const char* path, const char** kv);
