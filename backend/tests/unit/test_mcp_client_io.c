#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "services/ai/tools/mcp/mcp_client.h"

typedef struct {
    int    pipe_fd;
    size_t total_bytes;
} feeder_arg_t;

static void* huge_stream_feeder(void* arg) {
    feeder_arg_t* a = (feeder_arg_t*)arg;
    char chunk[4096];
    memset(chunk, 'X', sizeof(chunk));
    size_t written = 0;
    while (written < a->total_bytes) {
        size_t to_write = (a->total_bytes - written > sizeof(chunk)) ? sizeof(chunk) : (a->total_bytes - written);
        ssize_t w = write(a->pipe_fd, chunk, to_write);
        if (w <= 0) break;
        written += w;
    }
    close(a->pipe_fd);
    return NULL;
}

int main(void) {
    /* Ignore SIGPIPE so feeder does not kill test process when framer caps at 1MB */
    signal(SIGPIPE, SIG_IGN);
    printf("Running test_mcp_client_io...\n");

    mf_mcp_server_t s = {
        .transport = MCP_TRANSPORT_STDIO,
        .timeout_ms = 2000
    };
    mf_mcp_client_t* c = mf_mcp_client_new(&s);
    assert(c != NULL);
    assert(c->err_fd == -1);
    assert(c->framer.head == 0 && c->framer.tail == 0);
    assert(sizeof(c->last_error) >= 1024);

    int out_pipe[2];
    int in_pipe[2];
    assert(pipe(out_pipe) == 0);
    assert(pipe(in_pipe) == 0);
    c->read_fd = out_pipe[0];
    c->write_fd = in_pipe[1];

    /* Test 1: Single small frame */
    const char* frame1 = "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"status\":\"ok\"}}\n";
    assert(write(out_pipe[1], frame1, strlen(frame1)) == (ssize_t)strlen(frame1));

    char err[256] = {0};
    char* resp1 = mf_mcp_client_call_tool(c, "dummy", "{}", err, sizeof(err));
    assert(resp1 != NULL);
    assert(strstr(resp1, "\"status\":\"ok\"") != NULL);
    free(resp1);

    /* Test 2: Frame crossing 4KB buffer boundary (8KB payload) */
    size_t big_sz = 8192;
    char* big_frame = (char*)malloc(big_sz + 64);
    assert(big_frame != NULL);
    strcpy(big_frame, "{\"jsonrpc\":\"2.0\",\"id\":2,\"result\":{\"data\":\"");
    size_t prefix_len = strlen(big_frame);
    memset(big_frame + prefix_len, 'A', big_sz);
    big_frame[prefix_len + big_sz] = '\0';
    strcat(big_frame, "\"}}\n");

    assert(write(out_pipe[1], big_frame, strlen(big_frame)) == (ssize_t)strlen(big_frame));
    char* resp2 = mf_mcp_client_call_tool(c, "dummy", "{}", err, sizeof(err));
    assert(resp2 != NULL);
    assert(strlen(resp2) > big_sz);
    free(resp2);
    free(big_frame);

    /* Test 3: Multiple small frames in one buffer (framing cursor advancement) */
    const char* multi = "{\"jsonrpc\":\"2.0\",\"id\":3,\"result\":{\"idx\":3}}\n{\"jsonrpc\":\"2.0\",\"id\":4,\"result\":{\"idx\":4}}\n";
    assert(write(out_pipe[1], multi, strlen(multi)) == (ssize_t)strlen(multi));
    char* resp_m1 = mf_mcp_client_call_tool(c, "dummy", "{}", err, sizeof(err));
    assert(resp_m1 != NULL);
    assert(strstr(resp_m1, "\"idx\":3") != NULL);
    free(resp_m1);
    char* resp_m2 = mf_mcp_client_call_tool(c, "dummy", "{}", err, sizeof(err));
    assert(resp_m2 != NULL);
    assert(strstr(resp_m2, "\"idx\":4") != NULL);
    free(resp_m2);

    /* Close previous out_pipe ends before Test 4 to avoid descriptor leaks */
    close(out_pipe[1]);
    close(out_pipe[0]);

    /* Test 4: Frame exceeding 1MB cap safely intercepted without pipe deadlock */
    int huge_pipe[2];
    assert(pipe(huge_pipe) == 0);
    c->read_fd = huge_pipe[0];
    feeder_arg_t f_arg = { .pipe_fd = huge_pipe[1], .total_bytes = 1024 * 1024 + 16 };
    pthread_t f_th;
    assert(pthread_create(&f_th, NULL, huge_stream_feeder, &f_arg) == 0);

    char* resp3 = mf_mcp_client_call_tool(c, "dummy", "{}", err, sizeof(err));
    pthread_join(f_th, NULL);
    assert(resp3 == NULL);
    assert(strstr(err, "1MB limit") != NULL);

    /* Cleanly close huge_pipe and set c->read_fd to -1 to avoid double-close in free */
    close(huge_pipe[0]);
    c->read_fd = -1;
    close(in_pipe[0]);
    mf_mcp_client_free(c);
    printf("PASS: test_mcp_client_io completed successfully\n");
    return 0;
}
