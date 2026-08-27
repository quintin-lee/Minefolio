#include "services/file_parser.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>

static const char*
file_ext(const char* filename)
{
    const char* dot = strrchr(filename, '.');
    return dot ? dot + 1 : "";
}

static int
write_temp_file(
    const char* data, size_t len, const char* suffix, char* out_path, size_t out_path_len)
{
    snprintf(out_path, out_path_len, "/tmp/minefolio_upload_XXXXXX%s", suffix);
    int fd = mkstemp(out_path);
    if (fd < 0) {
        return -1;
    }
    ssize_t n = write(fd, data, len);
    close(fd);
    return (n == (ssize_t)len) ? 0 : -1;
}

static int
parse_txt(const char* data, size_t len, char* out, size_t out_len)
{
    size_t n = len < out_len - 1 ? len : out_len - 1;
    memcpy(out, data, n);
    out[n] = '\0';
    return 0;
}

static int
parse_csv(const char* data, size_t len, char* out, size_t out_len)
{
    size_t prefix = snprintf(out, 64, "CSV file (%zu bytes):\n", len);
    size_t n = len < out_len - prefix - 1 ? len : out_len - prefix - 1;
    memcpy(out + prefix, data, n);
    out[prefix + n] = '\0';
    return 0;
}

static int
parse_pdf(const char* data, size_t len, char* out, size_t out_len)
{
    char tmp_path[256];
    if (write_temp_file(data, len, ".pdf", tmp_path, sizeof(tmp_path)) != 0) {
        return -1;
    }

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "pdftotext -layout '%s' - 2>/dev/null", tmp_path);
    FILE* fp = popen(cmd, "r");
    if (!fp) {
        unlink(tmp_path);
        return -1;
    }

    size_t total = 0;
    char   buf[65536];
    while (total < out_len - 1 && fgets(buf, sizeof(buf), fp)) {
        size_t blen = strlen(buf);
        if (total + blen >= out_len - 1) {
            break;
        }
        memcpy(out + total, buf, blen);
        total += blen;
    }
    out[total] = '\0';
    pclose(fp);
    unlink(tmp_path);
    return total > 0 ? 0 : -1;
}

static int
parse_zip(const char* data, size_t len, char* out, size_t out_len)
{
    char tmp_path[256];
    if (write_temp_file(data, len, ".zip", tmp_path, sizeof(tmp_path)) != 0) {
        return -1;
    }

    /* List contents */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "unzip -l '%s' 2>&1 | head -30", tmp_path);
    FILE* fp = popen(cmd, "r");
    if (!fp) {
        unlink(tmp_path);
        return -1;
    }

    size_t total = 0;
    char   buf[65536];
    while (total < out_len - 1 && fgets(buf, sizeof(buf), fp)) {
        size_t blen = strlen(buf);
        if (total + blen >= out_len - 1) {
            break;
        }
        memcpy(out + total, buf, blen);
        total += blen;
    }
    out[total] = '\0';
    pclose(fp);
    unlink(tmp_path);
    return total > 0 ? 0 : -1;
}

int
file_parse(csilk_db_pool_t* pool,
           const char*      data,
           size_t           data_len,
           const char*      filename,
           char*            out,
           size_t           out_len)
{
    (void)pool;
    if (!data || data_len == 0 || !filename || !out || out_len == 0) {
        return -1;
    }

    const char* ext = file_ext(filename);

    if (strcmp(ext, "txt") == 0 || strcmp(ext, "log") == 0 || strcmp(ext, "md") == 0) {
        return parse_txt(data, data_len, out, out_len);
    } else if (strcmp(ext, "csv") == 0 || strcmp(ext, "tsv") == 0) {
        return parse_csv(data, data_len, out, out_len);
    } else if (strcmp(ext, "pdf") == 0) {
        return parse_pdf(data, data_len, out, out_len);
    } else if (strcmp(ext, "zip") == 0) {
        return parse_zip(data, data_len, out, out_len);
    }
    return -1;
}

char*
file_parse_to_string(
    csilk_db_pool_t* pool, const char* data, size_t data_len, const char* filename, size_t max_len)
{
    if (!data || data_len == 0 || !filename) {
        return NULL;
    }
    if (max_len == 0) {
        max_len = 50000;
    }
    char* buf = (char*)malloc(max_len);
    if (!buf) {
        return NULL;
    }
    if (file_parse(pool, data, data_len, filename, buf, max_len) != 0) {
        free(buf);
        return NULL;
    }
    return buf;
}
