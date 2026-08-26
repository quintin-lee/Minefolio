#include "common/csv_utils.h"
#include <stdio.h>
#include <string.h>

void
csv_escape(char* out, size_t out_size, const char* val)
{
    if (!val) {
        *out = '\0';
        return;
    }
    int needs_quote = 0;
    for (const char* p = val; *p; p++) {
        if (*p == ',' || *p == '"' || *p == '\n' || *p == '\r') {
            needs_quote = 1;
            break;
        }
    }
    if (!needs_quote) {
        snprintf(out, out_size, "%s", val);
        return;
    }
    size_t j = 0;
    out[j++] = '"';
    for (const char* p = val; *p && j < out_size - 2; p++) {
        if (*p == '"') {
            out[j++] = '"';
            out[j++] = '"';
        } else {
            out[j++] = *p;
        }
    }
    out[j++] = '"';
    out[j++] = '\0';
}

int
parse_csv_field(const char* line, size_t len, char* out, size_t out_size, size_t* chars_consumed)
{
    size_t pos = 0;
    if (pos >= len || line[pos] != '"') {
        while (pos < len && line[pos] != ',' && line[pos] != '\n' && line[pos] != '\r') {
            pos++;
        }
        size_t n = pos < out_size - 1 ? pos : out_size - 1;
        memcpy(out, line, n);
        out[n] = '\0';
        if (chars_consumed) {
            *chars_consumed = pos;
        }
        return 0;
    }
    pos++;
    size_t oi = 0;
    while (pos < len) {
        if (line[pos] == '"' && pos + 1 < len && line[pos + 1] == '"') {
            if (oi < out_size - 1) {
                out[oi++] = '"';
            }
            pos += 2;
        } else if (line[pos] == '"') {
            pos++;
            if (chars_consumed) {
                *chars_consumed = pos;
            }
            out[oi] = '\0';
            return 0;
        } else {
            if (oi < out_size - 1) {
                out[oi++] = line[pos];
            }
            pos++;
        }
    }
    out[oi] = '\0';
    if (chars_consumed) {
        *chars_consumed = pos;
    }
    return 0;
}

int
parse_csv_row(const char* line, size_t len, char out[12][512], int* count)
{
    size_t pos = 0;
    *count = 0;
    while (pos < len) {
        size_t consumed = 0;
        if (*count >= 12) {
            break;
        }
        parse_csv_field(line + pos, len - pos, out[*count], 512, &consumed);
        pos += consumed;
        (*count)++;
        if (pos >= len || line[pos] == '\n' || line[pos] == '\r') {
            if (pos < len && (line[pos] == '\r')) {
                pos++;
            }
            if (pos < len && line[pos] == '\n') {
                pos++;
            }
            break;
        }
        if (pos < len && line[pos] == ',') {
            pos++;
        }
    }
    return *count;
}
