#include "semaphore.h"
#include <stdio.h>

struct sem_context_t {
    size_t ref;
    struct spdk_sem_t* sem;
};

void sem_signal_complete(void* arg) {
    struct sem_context_t* ctx = (struct sem_context_t*)arg;
    struct spdk_sem_t* sem = ctx->sem;

    spdk_sem_signal(sem, ctx->ref);
    free(ctx);
    printf("sem_complete now:%d\n", spdk_sem_num(sem));
}

int
main(int argc, char **argv)
{
    spdk_sem_t sem;

    spdk_sem_init(&sem, 3);
    struct sem_context_t *ctx1 = (struct sem_context_t*)calloc(1, sizeof(struct sem_context_t));
    ctx1->ref = 1;
    ctx1->sem = &sem;
    spdk_sem_wait(&sem, 1, sem_signal_complete, ctx1);

    struct sem_context_t *ctx = (struct sem_context_t*)calloc(1, sizeof(struct sem_context_t));
    ctx->ref = 2;
    ctx->sem = &sem;
    spdk_sem_wait(&sem, 2, sem_signal_complete, ctx);

    struct sem_context_t *ctx2 = (struct sem_context_t*)calloc(1, sizeof(struct sem_context_t));
    ctx2->ref = 3;
    ctx2->sem = &sem;
    spdk_sem_wait(&sem, 3, sem_signal_complete, ctx2);

    struct sem_context_t *ctx3 = (struct sem_context_t*)calloc(1, sizeof(struct sem_context_t));
    ctx3->ref = 4;
    ctx3->sem = &sem;
    spdk_sem_wait(&sem, 4, sem_signal_complete, ctx3);

    struct sem_context_t *ctx4 = (struct sem_context_t*)calloc(1, sizeof(struct sem_context_t));
    ctx4->ref = 5;
    ctx4->sem = &sem;
    spdk_sem_wait(&sem, 5, sem_signal_complete, ctx4);

    printf("sem_signal\n");
    spdk_sem_signal(&sem, 1);

    struct sem_context_t *ctx5 = (struct sem_context_t*)calloc(1, sizeof(struct sem_context_t));
    ctx5->ref = 4;
    ctx5->sem = &sem;
    spdk_sem_wait(&sem, 4, sem_signal_complete, ctx5);

    struct sem_context_t *ctx6 = (struct sem_context_t*)calloc(1, sizeof(struct sem_context_t));
    ctx6->ref = 5;
    ctx6->sem = &sem;
    spdk_sem_wait(&sem, 5, sem_signal_complete, ctx6);

    spdk_sem_signal(&sem, 1);
    return 0;
}