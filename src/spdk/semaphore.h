#include <spdk/queue_extras.h>

typedef void (*spdk_sem_entry_fn)(void *arg);

struct waiter_entry {
	TAILQ_ENTRY(waiter_entry) next;
    int32_t units;
	spdk_sem_entry_fn fn;
	void * arg;
};

// 写法参考lib/ftl/ftl_core.h
struct spdk_sem_t {
    int32_t count;
    TAILQ_HEAD(, waiter_entry) waiters;
}

void spdk_sem_init(struct spdk_sem_t* sem, int32_t units) {
    TAILQ_INIT(&sem->waiters);
    sem->count = units;
}

bool spdk_sem_empty(struct spdk_sem_t* sem) {
    return LIST_EMPTY(&sem->waiters);
}

bool spdk_sem_has_units(struct spdk_sem_t* sem, int32_t units) {
    return sem->count >= 0 && sem->count >= units;
}

void spdk_sem_wait(struct spdk_sem_t* sem, int32_t units, spdk_sem_entry_fn fn, void *arg) {
    if (spdk_sem_has_units(sem, units) && spdk_sem_empty(sem)) {
        fn(arg);
    }
}

void spdk_sem_post(struct spdk_sem_t* sem, ) {

}