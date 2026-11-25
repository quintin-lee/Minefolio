#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "services/file_parser.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>

static const char*
file_ext(const char* filename)
{
    const char* dot = strrchr(filename, '.');
    return dot ? dot + 1 : "";
}

static bool
is_valid_file_suffix(const char* suffix)
{
    if (!suffix || !suffix[0]) {
        return true;
    }
    size_t len = strlen(suffix);
    if (len > 16 || suffix[0] != '.') {
        return false;
    }
    for (size_t i = 1; i < len; i++) {
        char c = suffix[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) {
            return false;
        }
    }
    return true;
}

int
file_parser_write_temp(
    const char* data, size_t len, const char* suffix, char* out_path, size_t out_path_len)
{
    if (!is_valid_file_suffix(suffix)) {
        return -1;
    }
    if (!suffix) {
        suffix = "";
    }
    size_t suffix_len = strlen(suffix);

    int r = snprintf(out_path, out_path_len, "/tmp/minefolio_upload_XXXXXX%s", suffix);
    if (r < 0 || (size_t)r >= out_path_len) {
        return -1;
    }

    /* mkstemps is supported by glibc/musl; takes length of suffix to keep XXXXXX replaceable */
    int fd = mkstemps(out_path, (int)suffix_len);
    if (fd < 0) {
        return -1;
    }
    ssize_t n = write(fd, data, len);
    close(fd);
    if (n != (ssize_t)len) {
        unlink(out_path);
        return -1;
    }
    return 0;
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
    if (out_len == 0) {
        return -1;
    }
    int r = snprintf(out, out_len, "CSV file (%zu bytes):\n", len);
    if (r < 0) {
        return -1;
    }
    size_t prefix = (size_t)r;
    if (prefix >= out_len - 1) {
        out[out_len - 1] = '\0';
        return 0;
    }
    size_t avail = out_len - prefix - 1;
    size_t n = len < avail ? len : avail;
    memcpy(out + prefix, data, n);
    out[prefix + n] = '\0';
    return 0;
}

static int
run_tool_capture(const char* prog,
                 char* const argv[],
                 char*       out,
                 size_t      out_len,
                 size_t      max_lines,
                 bool        merge_stderr)
{
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        return -1;
    }
    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        if (merge_stderr) {
            dup2(pipefd[1], STDERR_FILENO);
        } else {
            int devnull = open("/dev/null", O_WRONLY);
            if (devnull >= 0) {
                dup2(devnull, STDERR_FILENO);
                close(devnull);
            }
        }
        close(pipefd[1]);
        execvp(prog, argv);
        _exit(127);
    }

    close(pipefd[1]);
    FILE* fp = fdopen(pipefd[0], "r");
    if (!fp) {
        close(pipefd[0]);
        int st = 0;
        waitpid(pid, &st, 0);
        return -1;
    }

    size_t total = 0;
    size_t lines = 0;
    char   buf[4096];
    while (total < out_len - 1 && (max_lines == 0 || lines < max_lines) &&
           fgets(buf, sizeof(buf), fp)) {
        size_t blen = strlen(buf);
        if (total + blen >= out_len - 1) {
            break;
        }
        memcpy(out + total, buf, blen);
        total += blen;
        lines++;
    }
    out[total] = '\0';
    fclose(fp);
    int st = 0;
    waitpid(pid, &st, 0);
    return total > 0 ? 0 : -1;
}

static int
parse_pdf(const char* data, size_t len, char* out, size_t out_len)
{
    char tmp_path[256];
    if (file_parser_write_temp(data, len, ".pdf", tmp_path, sizeof(tmp_path)) != 0) {
        return -1;
    }
    char* argv[] = {"pdftotext", "-layout", tmp_path, "-", NULL};
    int   rc = run_tool_capture("pdftotext", argv, out, out_len, 0, false);
    unlink(tmp_path);
    return rc;
}

static int
parse_zip(const char* data, size_t len, char* out, size_t out_len)
{
    char tmp_path[256];
    if (file_parser_write_temp(data, len, ".zip", tmp_path, sizeof(tmp_path)) != 0) {
        return -1;
    }
    char* argv[] = {"unzip", "-l", tmp_path, NULL};
    int   rc = run_tool_capture("unzip", argv, out, out_len, 30, true);
    unlink(tmp_path);
    return rc;
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
