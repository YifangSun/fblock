#include <spdk/blob.h>
#include <spdk/blob_bdev.h>
#include <spdk/env.h>
#include <spdk/event.h>

#define BUF_LARGE_POOL_SIZE			1023

class appender {
public:
    struct spdk_mempool *buf_pool;


};