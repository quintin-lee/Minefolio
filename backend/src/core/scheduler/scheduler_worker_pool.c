#include "core/scheduler/scheduler_worker_pool.h"

#include <pthread.h>
#include <stdlib.h>

typedef struct scheduler_task_node {
    scheduler_task_func_t       func;
    void*                       arg;
    struct scheduler_task_node* next;
} scheduler_task_node_t;

struct scheduler_worker_pool {
    pthread_t*             threads;
    int                    num_threads;
    scheduler_task_node_t* head;
    scheduler_task_node_t* tail;
    pthread_mutex_t        lock;
    pthread_cond_t         cond;
    bool                   shutdown;
};

static void*
worker_thread(void* arg)
{
    scheduler_worker_pool_t* pool = (scheduler_worker_pool_t*)arg;

    while (1) {
        pthread_mutex_lock(&pool->lock);
        while (!pool->head && !pool->shutdown) {
            pthread_cond_wait(&pool->cond, &pool->lock);
        }

        if (pool->shutdown && !pool->head) {
            pthread_mutex_unlock(&pool->lock);
            break;
        }

        scheduler_task_node_t* node = pool->head;
        if (node) {
            pool->head = node->next;
            if (!pool->head) {
                pool->tail = NULL;
            }
        }
        pthread_mutex_unlock(&pool->lock);

        if (node) {
            if (node->func) {
                node->func(node->arg);
            }
            free(node);
        }
    }

    return NULL;
}

scheduler_worker_pool_t*
scheduler_worker_pool_create(int num_threads)
{
    if (num_threads <= 0) {
        num_threads = 3;
    }

    scheduler_worker_pool_t* pool =
        (scheduler_worker_pool_t*)calloc(1, sizeof(scheduler_worker_pool_t));
    if (!pool) {
        return NULL;
    }

    pool->num_threads = num_threads;
    pool->threads = (pthread_t*)calloc((size_t)num_threads, sizeof(pthread_t));
    if (!pool->threads) {
        free(pool);
        return NULL;
    }

    pthread_mutex_init(&pool->lock, NULL);
    pthread_cond_init(&pool->cond, NULL);

    for (int i = 0; i < num_threads; i++) {
        pthread_create(&pool->threads[i], NULL, worker_thread, pool);
    }

    return pool;
}

int
scheduler_worker_pool_submit(scheduler_worker_pool_t* pool, scheduler_task_func_t func, void* arg)
{
    if (!pool || !func) {
        return -1;
    }

    scheduler_task_node_t* node = (scheduler_task_node_t*)malloc(sizeof(scheduler_task_node_t));
    if (!node) {
        return -1;
    }
    node->func = func;
    node->arg = arg;
    node->next = NULL;

    pthread_mutex_lock(&pool->lock);
    if (pool->shutdown) {
        pthread_mutex_unlock(&pool->lock);
        free(node);
        return -1;
    }

    if (!pool->head) {
        pool->head = node;
        pool->tail = node;
    } else {
        pool->tail->next = node;
        pool->tail = node;
    }

    pthread_cond_signal(&pool->cond);
    pthread_mutex_unlock(&pool->lock);
    return 0;
}

void
scheduler_worker_pool_destroy(scheduler_worker_pool_t* pool)
{
    if (!pool) {
        return;
    }

    pthread_mutex_lock(&pool->lock);
    pool->shutdown = true;
    pthread_cond_broadcast(&pool->cond);
    pthread_mutex_unlock(&pool->lock);

    for (int i = 0; i < pool->num_threads; i++) {
        pthread_join(pool->threads[i], NULL);
    }

    /* Free remaining queue */
    scheduler_task_node_t* cur = pool->head;
    while (cur) {
        scheduler_task_node_t* next = cur->next;
        free(cur);
        cur = next;
    }

    pthread_mutex_destroy(&pool->lock);
    pthread_cond_destroy(&pool->cond);
    free(pool->threads);
    free(pool);
}
