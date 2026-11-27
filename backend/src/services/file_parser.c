#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "services/file_parser.h"
#include "common/db.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

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
    if (!out_path || out_path_len == 0 || (!data && len > 0)) {
        return -1;
    }
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
    size_t written = 0;
    while (written < len) {
        ssize_t n = write(fd, data + written, len - written);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            close(fd);
            unlink(out_path);
            return -1;
        }
        if (n == 0) {
            close(fd);
            unlink(out_path);
            return -1;
        }
        written += (size_t)n;
    }
    close(fd);
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

static void
close_inherited_fds_child(long max_fd)
{
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 34))
    (void)max_fd;
    closefrom(3);
#else
    /* Fallback strictly bounded to pre-fork max_fd; never call opendir() after fork(). */
    if (max_fd < 3 || max_fd > 65536) {
        max_fd = 65536;
    }
    for (int fd = 3; fd < (int)max_fd; fd++) {
        close(fd);
    }
#endif
}

static int64_t
monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000LL + (int64_t)(ts.tv_nsec / 1000000LL);
}

int
file_parser_run_tool_capture_timeout(const char* prog,
                                     char* const argv[],
                                     char*       out,
                                     size_t      out_len,
                                     size_t      max_lines,
                                     bool        merge_stderr,
                                     int         timeout_ms)
{
    if (!prog || !argv || !out || out_len == 0) {
        return -1;
    }

    long max_fd = sysconf(_SC_OPEN_MAX);
    if (max_fd < 0 || max_fd > 65536) {
        max_fd = 65536;
    }

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
        signal(SIGPIPE, SIG_DFL);
        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
            _exit(127);
        }
        if (merge_stderr) {
            if (dup2(pipefd[1], STDERR_FILENO) < 0) {
                _exit(127);
            }
        } else {
            int devnull = open("/dev/null", O_WRONLY);
            if (devnull >= 0) {
                dup2(devnull, STDERR_FILENO);
                if (devnull > STDERR_FILENO) {
                    close(devnull);
                }
            }
        }
        if (pipefd[1] > STDERR_FILENO) {
            close(pipefd[1]);
        }
        close_inherited_fds_child(max_fd);
        execvp(prog, argv);
        _exit(127);
    }

    close(pipefd[1]);
    int flags = fcntl(pipefd[0], F_GETFL, 0);
    if (flags >= 0) {
        (void)fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);
    }

    int     effective_timeout = timeout_ms > 0 ? timeout_ms : 15000;
    int64_t deadline_ms = monotonic_ms() + (int64_t)effective_timeout;
    size_t  total = 0;
    size_t  lines = 0;
    bool    timed_out = false;
    bool    eof_reached = false;
    bool    done = false;

    while (!done && total < out_len - 1 && (max_lines == 0 || lines < max_lines)) {
        int64_t now_ms = monotonic_ms();
        int64_t rem_ms = deadline_ms - now_ms;
        if (rem_ms <= 0) {
            timed_out = true;
            break;
        }
        int poll_timeout = rem_ms > INT_MAX ? INT_MAX : (int)rem_ms;

        struct pollfd pfd = {.fd = pipefd[0], .events = POLLIN, .revents = 0};
        int           pr = poll(&pfd, 1, poll_timeout);
        if (pr < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (pr == 0) {
            timed_out = true;
            break;
        }

        if (pfd.revents & (POLLIN | POLLHUP)) {
            for (;;) {
                char    buf[4096];
                ssize_t r = read(pipefd[0], buf, sizeof(buf));
                if (r > 0) {
                    for (ssize_t i = 0; i < r; i++) {
                        if (total >= out_len - 1) {
                            done = true;
                            break;
                        }
                        out[total++] = buf[i];
                        if (buf[i] == '\n') {
                            lines++;
                            if (max_lines > 0 && lines >= max_lines) {
                                done = true;
                                break;
                            }
                        }
                    }
                    if (done) {
                        break;
                    }
                } else if (r == 0) {
                    eof_reached = true;
                    done = true;
                    break;
                } else {
                    if (errno == EINTR) {
                        continue;
                    }
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    }
                    done = true;
                    break;
                }
            }
        } else if (pfd.revents & (POLLERR | POLLNVAL)) {
            break;
        }
    }

    close(pipefd[0]);
    int st = 0;
    if (timed_out) {
        kill(pid, SIGKILL);
        while (waitpid(pid, &st, 0) < 0) {
            if (errno != EINTR) {
                break;
            }
        }
    } else if (!eof_reached) {
        /* Early exit (e.g. out_len / max_lines cap): bound child wait so parent never blocks indefinitely. */
        kill(pid, SIGTERM);
        bool reaped = false;
        for (int i = 0; i < 20; i++) {
            pid_t w = waitpid(pid, &st, WNOHANG);
            if (w == pid || (w < 0 && errno != EINTR)) {
                reaped = true;
                break;
            }
            struct timespec ts = {0, 10000000L}; /* 10ms */
            nanosleep(&ts, NULL);
        }
        if (!reaped) {
            kill(pid, SIGKILL);
            while (waitpid(pid, &st, 0) < 0) {
                if (errno != EINTR) {
                    break;
                }
            }
        }
    } else {
        while (waitpid(pid, &st, 0) < 0) {
            if (errno != EINTR) {
                break;
            }
        }
    }

    if (timed_out) {
        out[0] = '\0';
        return -1;
    }
    out[total] = '\0';
    return total > 0 ? 0 : -1;
}

static int
run_tool_capture(const char* prog,
                 char* const argv[],
                 char*       out,
                 size_t      out_len,
                 size_t      max_lines,
                 bool        merge_stderr)
{
    return file_parser_run_tool_capture_timeout(
        prog, argv, out, out_len, max_lines, merge_stderr, 15000);
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
