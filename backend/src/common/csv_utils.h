#pragma once
#include <stddef.h>
void csv_escape(char* out, size_t out_size, const char* val);
int parse_csv_field(const char* line, size_t len, char* out, size_t out_size, size_t* chars_consumed);
int parse_csv_row(const char* line, size_t len, char out[12][512], int* count);
