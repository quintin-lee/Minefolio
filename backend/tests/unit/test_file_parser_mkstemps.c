#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "services/file_parser.h"

extern int file_parser_run_tool_capture_timeout(const char* prog,
                                                char* const argv[],
                                                char*       out,
                                                size_t      out_len,
                                                size_t      max_lines,
                                                bool        merge_stderr,
                                                int         timeout_ms);

static long long
now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + (long long)(ts.tv_nsec / 1000000LL);
}

int main(void) {
    printf("Running test_file_parser_mkstemps...\n");

    const char* sample_data = "%PDF-1.4 sample content";
    char out_path[256];

    /* 1. Test .pdf suffix */
    int rc = file_parser_write_temp(sample_data, strlen(sample_data), ".pdf", out_path, sizeof(out_path));
    assert(rc == 0);
    assert(strstr(out_path, ".pdf") != NULL);
    assert(access(out_path, R_OK) == 0);
    unlink(out_path);

    /* 2. Test .zip suffix */
    rc = file_parser_write_temp("PK\x03\x04", 4, ".zip", out_path, sizeof(out_path));
    assert(rc == 0);
    assert(strstr(out_path, ".zip") != NULL);
    assert(access(out_path, R_OK) == 0);
    unlink(out_path);

    /* 3. Negative tests: invalid suffix with path traversal or injection */
    assert(file_parser_write_temp(sample_data, 10, "../../etc/passwd", out_path, sizeof(out_path)) == -1);
    assert(file_parser_write_temp(sample_data, 10, ".p;d'f", out_path, sizeof(out_path)) == -1);
    assert(file_parser_write_temp(sample_data, 10, ".toolong_extension_overflow_check", out_path, sizeof(out_path)) == -1);

    /* 4. Full buffer write verification (large payload) */
    {
        size_t big_len = 512 * 1024;
        char*  big_buf = (char*)malloc(big_len);
        assert(big_buf != NULL);
        for (size_t i = 0; i < big_len; i++) {
            big_buf[i] = (char)('A' + (i % 26));
        }

        rc = file_parser_write_temp(big_buf, big_len, ".txt", out_path, sizeof(out_path));
        assert(rc == 0);

        int fd = open(out_path, O_RDONLY);
        assert(fd >= 0);
        char*  read_buf = (char*)malloc(big_len);
        assert(read_buf != NULL);
        size_t total_read = 0;
        while (total_read < big_len) {
            ssize_t r = read(fd, read_buf + total_read, big_len - total_read);
            if (r < 0 && errno == EINTR) {
                continue;
            }
            assert(r > 0);
            total_read += (size_t)r;
        }
        /* Confirm EOF */
        char extra;
        assert(read(fd, &extra, 1) == 0);
        close(fd);
        assert(total_read == big_len);
        assert(memcmp(big_buf, read_buf, big_len) == 0);

        free(read_buf);
        free(big_buf);
        unlink(out_path);
    }

    /* 5. Normal tool capture with max_lines */
    {
        char  out[256] = {0};
        char* argv[] = {"sh", "-c", "printf 'alpha\\nbeta\\ngamma\\n'", NULL};
        rc = file_parser_run_tool_capture_timeout("sh", argv, out, sizeof(out), 2, false, 2000);
        assert(rc == 0);
        assert(strcmp(out, "alpha\nbeta\n") == 0);
    }

    /* 6. Child sanitation: close inherited FDs >= 3 and restore SIGPIPE to SIG_DFL */
    {
        void (*prev_sigpipe)(int) = signal(SIGPIPE, SIG_IGN);
        int dummy[2];
        assert(pipe(dummy) == 0);

        char cmd[256];
        snprintf(cmd,
                 sizeof(cmd),
                 "if [ -e /proc/self/fd/%d ] || [ -e /proc/self/fd/%d ]; then echo LEAK; "
                 "else t=$(trap); case \"$t\" in *SIGPIPE*) echo SIGPIPE_IGN;; *) echo CLEAN;; esac; fi",
                 dummy[0],
                 dummy[1]);

        char  out[256] = {0};
        char* argv[] = {"sh", "-c", cmd, NULL};
        rc = file_parser_run_tool_capture_timeout("sh", argv, out, sizeof(out), 0, false, 2000);
        close(dummy[0]);
        close(dummy[1]);
        signal(SIGPIPE, prev_sigpipe);

        assert(rc == 0);
        assert(strstr(out, "CLEAN") != NULL);
        assert(strstr(out, "LEAK") == NULL);
        assert(strstr(out, "SIGPIPE_IGN") == NULL);
    }

    /* 7. Stalled child timeout: kills child with SIGKILL and reaps without zombie leak */
    {
        char pid_path[256] = {0};
        assert(file_parser_write_temp("", 0, ".pid", pid_path, sizeof(pid_path)) == 0);

        char      out[256] = {0};
        char*     argv[] = {"sh", "-c", "echo $$ > \"$1\"; exec sleep 10", "_", pid_path, NULL};
        long long t0 = now_ms();
        rc = file_parser_run_tool_capture_timeout("sh", argv, out, sizeof(out), 0, false, 400);
        long long elapsed = now_ms() - t0;

        assert(rc == -1);
        assert(elapsed < 3000);

        FILE* pf = fopen(pid_path, "r");
        assert(pf != NULL);
        long child_pid = 0;
        int  scanned = fscanf(pf, "%ld", &child_pid);
        fclose(pf);
        unlink(pid_path);

        assert(scanned == 1);
        assert(child_pid > 0);
        /* Process must not exist (not alive, not zombie) after waitpid */
        assert(kill((pid_t)child_pid, 0) == -1 && errno == ESRCH);
    }

    printf("PASS: test_file_parser_mkstemps completed successfully\n");
    return 0;
}
