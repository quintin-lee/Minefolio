#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "services/ai/tools/mcp/mcp_client.h"

typedef struct {
    int err_pipe_wr;
    int out_pipe_wr;
} child_sim_t;

static void* verbose_stderr_feeder(void* arg) {
    child_sim_t* sim = (child_sim_t*)arg;
    
    /* 1. First write distinct diagnostic error string */
    const char* header = "stdio child stderr: Module Crashed on Import!\n";
    assert(write(sim->err_pipe_wr, header, strlen(header)) == (ssize_t)strlen(header));

    /* 2. Write 80 KB padding to prove parent drains without deadlocking child on 64KB buffer */
    char chunk[4096];
    memset(chunk, 'E', sizeof(chunk));
    size_t total = 80 * 1024;
    size_t written = 0;
    while (written < total) {
        size_t take = (total - written > sizeof(chunk)) ? sizeof(chunk) : (total - written);
        ssize_t w = write(sim->err_pipe_wr, chunk, take);
        if (w <= 0) break;
        written += w;
    }
    
    /* Simulate child exit: close stderr, then close stdout without sending JSON */
    close(sim->err_pipe_wr);
    close(sim->out_pipe_wr);
    return NULL;
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    printf("Running test_mcp_stderr_capture...\n");
    mf_mcp_server_t s = {
        .transport = MCP_TRANSPORT_STDIO,
        .timeout_ms = 2000
    };
    mf_mcp_client_t* c = mf_mcp_client_new(&s);
    assert(c != NULL);

    int out_pipe[2];
    int in_pipe[2];
    int err_pipe[2];
    assert(pipe(out_pipe) == 0);
    assert(pipe(in_pipe) == 0);
    assert(pipe(err_pipe) == 0);
    fcntl(err_pipe[0], F_SETFL, O_NONBLOCK);

    c->read_fd = out_pipe[0];
    c->write_fd = in_pipe[1];
    c->err_fd = err_pipe[0];

    child_sim_t sim = {
        .err_pipe_wr = err_pipe[1],
        .out_pipe_wr = out_pipe[1]
    };
    pthread_t th;
    assert(pthread_create(&th, NULL, verbose_stderr_feeder, &sim) == 0);

    char err[512] = {0};
    char* resp = mf_mcp_client_call_tool(c, "dummy", "{}", err, sizeof(err));
    pthread_join(th, NULL);

    assert(resp == NULL);
    assert(strlen(c->last_error) > 0);
    assert(strstr(err, "stdio child stderr:") != NULL || strstr(err, "Module Crashed") != NULL);

    close(in_pipe[0]);
    mf_mcp_client_free(c);
    printf("PASS: test_mcp_stderr_capture completed successfully\n");
    return 0;
}
