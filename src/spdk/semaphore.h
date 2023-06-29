#pragma once

#include <spdk/queue.h>
#include <stddef.h>
#include <stdlib.h>

typedef void (*spdk_sem_entry_fn)(void *arg);

struct waiter_entry {
	TAILQ_ENTRY(waiter_entry) next;
    size_t units;
	spdk_sem_entry_fn fn;
	void * arg;
};

// 写法参考lib/ftl/ftl_core.h
struct spdk_sem_t {
    size_t count;
    TAILQ_HEAD(, waiter_entry) waiters;
};

void spdk_sem_init(struct spdk_sem_t* sem, size_t units);

inline size_t spdk_sem_num(struct spdk_sem_t* sem) {
    return sem->count;
}

bool spdk_sem_empty(struct spdk_sem_t* sem);

bool spdk_sem_has_units(struct spdk_sem_t* sem, size_t units);

// 函数运行时，会拿走计数器的值，并且不会自动放回。
// 需要在函数结束后，由用户自己主动调用 spdk_sem_signal() 放回。
void spdk_sem_wait(struct spdk_sem_t* sem, size_t units, spdk_sem_entry_fn fn, void *arg);

void spdk_sem_signal(struct spdk_sem_t* sem, size_t units);