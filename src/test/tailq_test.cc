#include <spdk/queue.h>

struct waiter_entry {
	TAILQ_ENTRY(waiter_entry) next;
    int32_t index;
};

// 写法参考lib/ftl/ftl_core.h
struct tailq_context_t {
    TAILQ_HEAD(, waiter_entry) waiters;
}

int
main(int argc, char **argv)
{
    tailq_context_t tailq;

    TAILQ_INSERT_TAIL(&sem->waiters, entry, next);
    TAILQ_FOREACH(history, &g_run_counter_history, link) {
		if ((history->poller_id == poller_id) && (history->thread_id == thread_id)) {
			return history->last_busy_counter;
		}
	}

    return 0;
}