#include "semaphore.h"
#include <stdio.h>

void spdk_sem_init(struct spdk_sem_t* sem, size_t units) {
    TAILQ_INIT(&sem->waiters);
    sem->count = units;
}

bool spdk_sem_empty(struct spdk_sem_t* sem) {
    return TAILQ_EMPTY(&sem->waiters);
}

bool spdk_sem_has_units(struct spdk_sem_t* sem, size_t units) {
    return sem->count >= 0 && sem->count >= units;
}

void spdk_sem_wait(struct spdk_sem_t* sem, size_t units, spdk_sem_entry_fn fn, void *arg) {
    if (spdk_sem_has_units(sem, units) && spdk_sem_empty(sem)) {
        sem->count -= units;
        fn(arg);
        // printf("run\n");
        return;
    }
    
    struct waiter_entry *entry = (struct waiter_entry*)calloc(1, sizeof(struct waiter_entry));
    entry->units = units;
    entry->fn = fn;
    entry->arg = arg;
    // printf("insert\n");
    TAILQ_INSERT_TAIL(&sem->waiters, entry, next);
}

void spdk_sem_signal(struct spdk_sem_t* sem, size_t units) {
    struct waiter_entry * front;
    sem->count += units;
    // printf("put %d\n", units);
    while (front = TAILQ_FIRST(&sem->waiters),
        front && spdk_sem_has_units(sem, front->units)
        && !spdk_sem_empty(sem)) 
    {
        TAILQ_REMOVE(&sem->waiters, front, next);
        printf("signal run waiter, sem:%d takes:%d\n", sem->count, front->units);
        sem->count -= front->units;
        front->fn(front->arg);
        // printf("run\n");
        free(front);
        // printf("free\n");
    }
}