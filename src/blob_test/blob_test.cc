/*-
 *   BSD LICENSE
 *
 *   Copyright (c) Intel Corporation.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "spdk/stdinc.h"
#include "spdk/bdev.h"
#include "spdk/env.h"
#include "spdk/event.h"
#include "spdk/blob_bdev.h"
#include "spdk/blob.h"
#include "spdk/log.h"
#include "spdk/string.h"

#include <time.h>
#include <stdlib.h>

#define BLOB_CLUSTERS 50000
#define CLUSTER_SIZE 1 << 20 // 1Mb

#define WRITE_NUM 1024

// 每次写8个units，就是4k
#define BLOCK_UNITS 8

/*
 * We'll use this struct to gather housekeeping hello_context to pass between
 * our events and callbacks.
 */
struct hello_context_t {
	struct spdk_blob_store *bs;
	struct spdk_blob *blob;
	spdk_blob_id blobid;

	struct spdk_io_channel *channel;
	char *read_buff;
	char *write_buff;
	uint64_t io_unit_size;
	char* bdev_name;
	int blob_num;

	int blob_size;
	int write_size;
	int block_num;

	int write_total;
	int write_count;
	uint64_t start;
	int rc;
};


struct write_ctx_t {
	struct hello_context_t* hello_ctx;
	int idx;
	int max;
	uint64_t start, end;
};


static inline double env_ticks_to_secs(uint64_t j)
{
	return (double)j / spdk_get_ticks_hz();
}

static inline double env_ticks_to_msecs(uint64_t j)
{
	return env_ticks_to_secs(j) * 1000;
}

static inline double env_ticks_to_usecs(uint64_t j)
{
	return env_ticks_to_secs(j) * 1000 * 1000;
}

static void
write_blob_iterates(struct hello_context_t *hello_context);


uint64_t create_blob_tsc, create_snap_tsc;

uint64_t write_blob_tsc, write_snap_tsc;

/*
 * Free up memory that we allocated.
 */
static void
hello_cleanup(struct hello_context_t *hello_context)
{
	spdk_free(hello_context->read_buff);
	spdk_free(hello_context->write_buff);
	free(hello_context);
}

/*
 * Callback routine for the blobstore unload.
 */
static void
unload_complete(void *cb_arg, int bserrno)
{
	struct hello_context_t *hello_context = (struct hello_context_t *)cb_arg;

	SPDK_NOTICELOG("entry\n");
	if (bserrno) {
		SPDK_ERRLOG("Error %d unloading the bobstore\n", bserrno);
		hello_context->rc = bserrno;
	}

	spdk_app_stop(hello_context->rc);
}

/*
 * Unload the blobstore, cleaning up as needed.
 */
static void
unload_bs(struct hello_context_t *hello_context, char *msg, int bserrno)
{
	if (bserrno) {
		SPDK_ERRLOG("%s (err %d)\n", msg, bserrno);
		hello_context->rc = bserrno;
	}
	if (hello_context->bs) {
		if (hello_context->channel) {
			spdk_bs_free_io_channel(hello_context->channel);
		}
		spdk_bs_unload(hello_context->bs, unload_complete, hello_context);
	} else {
		spdk_app_stop(bserrno);
	}
}




static void
delete_complete(void *arg1, int bserrno)
{
	struct hello_context_t *hello_context = (struct hello_context_t *)arg1;

	SPDK_NOTICELOG("entry\n");
	if (bserrno) {
		unload_bs(hello_context, "Error in delete completion",
			  bserrno);
		return;
	}

	/* We're all done, we can unload the blobstore. */
	unload_bs(hello_context, "", 0);
}

static void
delete_blob(void *arg1, int bserrno)
{
	struct hello_context_t *hello_context = (struct hello_context_t *)arg1;

	SPDK_NOTICELOG("entry\n");
	if (bserrno) {
		unload_bs(hello_context, "Error in close completion",
			  bserrno);
		return;
	}

	spdk_bs_delete_blob(hello_context->bs, hello_context->blobid,
			    delete_complete, hello_context);
}


static void
write_iterates_continue(void *arg1, int bserrno) {
	struct write_ctx_t *ctx = (struct write_ctx_t *)arg1;
	struct hello_context_t *hello_context = ctx->hello_ctx;

	if (bserrno) {
		unload_bs(hello_context, "Error in write blob iterates completion",
			  bserrno);
		return;
	}

    // SPDK_NOTICELOG("writed %d blob id:%" PRIu64 " \n", 
    //         ctx->idx, hello_context->blobids[ctx->idx]);
	ctx->idx++;
	if (ctx->idx < ctx->max) {
		int offset = rand() % (hello_context->block_num - 1);
		offset *= BLOCK_UNITS;
		// SPDK_NOTICELOG("blob write start:%d length:%d \n", ctx->idx * BLOCK_UNITS, BLOCK_UNITS);
		spdk_blob_io_write(hello_context->blob, hello_context->channel,
			   hello_context->write_buff,
			   offset, BLOCK_UNITS, write_iterates_continue, ctx);
	} else {
		uint64_t now = spdk_get_ticks();
		double us = env_ticks_to_usecs(now - ctx->start);
		SPDK_NOTICELOG("iterates write %d block, total time: %lf us\n", ctx->idx, us);

		spdk_blob_close(hello_context->blob, delete_blob, hello_context);
		free(ctx);
		// spdk_app_stop(-1);
	}
}

static void
blob_write_iterates(struct hello_context_t *hello_context) {
	struct write_ctx_t *ctx = NULL;

	ctx = (struct write_ctx_t*)calloc(1, sizeof(struct write_ctx_t));
	ctx->hello_ctx = hello_context;
	ctx->idx = 0;
	ctx->max = 100000;
	// ctx->max = BLOB_CLUSTERS * 1048576 / (hello_context->io_unit_size * BLOCK_UNITS);
	ctx->start = spdk_get_ticks();
    hello_context->channel = spdk_bs_alloc_io_channel(hello_context->bs);
    hello_context->write_buff = (char*)spdk_malloc(512 * BLOCK_UNITS,
                    0x1000, NULL, SPDK_ENV_LCORE_ID_ANY,
                    SPDK_MALLOC_DMA);
	memset(hello_context->write_buff, 0x5a, 512 * BLOCK_UNITS);

	hello_context->blob_size = BLOB_CLUSTERS * CLUSTER_SIZE;
	// SPDK_NOTICELOG("BLOB_CLUSTERS:%d CLUSTER_SIZE:%d blob_size:%d\n", 
	// 	BLOB_CLUSTERS, CLUSTER_SIZE, blob_size);

	hello_context->write_size = hello_context->io_unit_size * BLOCK_UNITS;
	// SPDK_NOTICELOG("io_unit_size:%d BLOCK_UNITS:%d write_size:%d\n", 
	// 	hello_context->io_unit_size, BLOCK_UNITS, write_size);

	hello_context->block_num = hello_context->blob_size / hello_context->write_size;
	// SPDK_NOTICELOG("blob_size:%d write_size:%d block_num:%d\n", 
	// 	blob_size, write_size, block_num);

	
	int offset = rand() % hello_context->block_num;
	offset *= BLOCK_UNITS;
	// SPDK_NOTICELOG("blob write start:%d length:%d \n", offset, BLOCK_UNITS);
	spdk_blob_io_write(hello_context->blob, hello_context->channel,
			   hello_context->write_buff,
			   offset, BLOCK_UNITS, write_iterates_continue, ctx);
}

static void
write_parallel_complete(void *arg1, int bserrno)
{
	struct hello_context_t *hello_context = (struct hello_context_t *)arg1;

	// SPDK_NOTICELOG("entry\n");
	if (bserrno) {
		unload_bs(hello_context, "Error in write parallel completion",
			  bserrno);
		return;
	}
	hello_context->write_count++;

	// 并行写结束
	if (hello_context->write_count == hello_context->write_total) {
		uint64_t now = spdk_get_ticks();
		double us = env_ticks_to_usecs(now - hello_context->start);
		SPDK_NOTICELOG("parallel write %d block, time: %lf\n", hello_context->write_count, us);

		spdk_blob_close(hello_context->blob, delete_blob, hello_context);
	}	
}

static void
blob_write_parallel(struct hello_context_t *hello_context)
{
	// SPDK_NOTICELOG("blob_write_parallel entry\n");
	int block_num, blob_size, write_size;

	/*
	 * Buffers for data transfer need to be allocated via SPDK. We will
	 * transfer 1 io_unit of 4K aligned data at offset 0 in the blob.
	 */
	hello_context->write_buff = (char*)spdk_malloc(512 * BLOCK_UNITS,
				0x1000, NULL, SPDK_ENV_LCORE_ID_ANY,
				SPDK_MALLOC_DMA);
    memset(hello_context->write_buff, 0x5a, 512 * BLOCK_UNITS);
	if (hello_context->write_buff == NULL) {
		unload_bs(hello_context, "Error in allocating memory",
			  -ENOMEM);
		return;
	}

	/* Now we have to allocate a channel. */
	hello_context->channel = spdk_bs_alloc_io_channel(hello_context->bs);
	if (hello_context->channel == NULL) {
		unload_bs(hello_context, "Error in allocating channel",
			  -ENOMEM);
		return;
	}

	hello_context->blob_size = BLOB_CLUSTERS * CLUSTER_SIZE;
	// SPDK_NOTICELOG("BLOB_CLUSTERS:%d CLUSTER_SIZE:%d blob_size:%d\n", 
	// 	BLOB_CLUSTERS, CLUSTER_SIZE, blob_size);

	hello_context->write_size = hello_context->io_unit_size * BLOCK_UNITS;
	// SPDK_NOTICELOG("io_unit_size:%d BLOCK_UNITS:%d write_size:%d\n", 
	// 	hello_context->io_unit_size, BLOCK_UNITS, write_size);

	hello_context->block_num = hello_context->blob_size / hello_context->write_size;
	// SPDK_NOTICELOG("blob_size:%d write_size:%d block_num:%d\n", 
	// 	blob_size, write_size, block_num);

	hello_context->write_total = std::min(block_num, WRITE_NUM);
	hello_context->start = spdk_get_ticks();

	for (uint32_t i = 0; i < hello_context->write_total; i++) {
		// SPDK_NOTICELOG("parallel write start:%d length:%d \n", i * BLOCK_UNITS, BLOCK_UNITS);
		int offset = rand() % (hello_context->block_num - 1);
		offset *= BLOCK_UNITS;
		spdk_blob_io_write(hello_context->blob, hello_context->channel,
			   hello_context->write_buff,
			   offset, BLOCK_UNITS, write_parallel_complete, hello_context);
	}
}

static void
open_complete(void *cb_arg, struct spdk_blob *blob, int bserrno)
{
	struct hello_context_t *hello_context = (struct hello_context_t *)cb_arg;
	uint64_t free = 0;

	SPDK_NOTICELOG("open entry\n");
	if (bserrno) {
		unload_bs(hello_context, "Error in open completion",
			  bserrno);
		return;
	}

	hello_context->blob = blob;

	blob_write_iterates(hello_context);
}


static void
blob_create_complete(void *arg1, spdk_blob_id blobid, int bserrno)
{
	struct hello_context_t *hello_context = (struct hello_context_t *)arg1;

	SPDK_NOTICELOG("entry\n");
	if (bserrno) {
		unload_bs(hello_context, "Error in blob create callback",
			  bserrno);
		return;
	}

	hello_context->blobid = blobid;
	SPDK_NOTICELOG("new blob id %" PRIu64 "\n", hello_context->blobid);

	/* We have to open the blob before we can do things like resize. */
	spdk_bs_open_blob(hello_context->bs, hello_context->blobid,
			  open_complete, hello_context);
}

static void
create_blob(struct hello_context_t *hello_context)
{
	SPDK_NOTICELOG("entry\n");

	struct spdk_blob_opts opts;
	spdk_blob_opts_init(&opts, sizeof(opts));
	opts.num_clusters = BLOB_CLUSTERS;
	spdk_bs_create_blob_ext(hello_context->bs, &opts, blob_create_complete, hello_context);
}

/*
 * Callback function for initializing the blobstore.
 */
static void
bs_init_complete(void *cb_arg, struct spdk_blob_store *bs,
		 int bserrno)
{
	struct hello_context_t *hello_context = (struct hello_context_t *)cb_arg;
    uint64_t free = 0;

	SPDK_NOTICELOG("init entry\n");
	if (bserrno) {
		unload_bs(hello_context, "Error initing the blobstore",
			  bserrno);
		return;
	}

	hello_context->bs = bs;
	SPDK_NOTICELOG("blobstore: %p\n", hello_context->bs);
	/*
	 * We will use the io_unit size in allocating buffers, etc., later
	 * so we'll just save it in out context buffer here.
	 */
	hello_context->io_unit_size = spdk_bs_get_io_unit_size(hello_context->bs);
    free = spdk_bs_free_cluster_count(hello_context->bs);
    SPDK_NOTICELOG("blobstore has FREE clusters of %" PRIu64 "\n", free);

	create_blob(hello_context);
}

static void
base_bdev_event_cb(enum spdk_bdev_event_type type, struct spdk_bdev *bdev,
		   void *event_ctx)
{
	SPDK_WARNLOG("Unsupported bdev event: type %d\n", type);
}

/*
 * Our initial event that kicks off everything from main().
 */
static void
hello_start(void *arg1)
{
	struct hello_context_t *hello_context = (struct hello_context_t *)arg1;
	struct spdk_bs_dev *bs_dev = NULL;
	int rc;

	SPDK_NOTICELOG("entry\n");

	/*
	 * In this example, use our malloc (RAM) disk configured via
	 * hello_blob.json that was passed in when we started the
	 * SPDK app framework.
	 *
	 * spdk_bs_init() requires us to fill out the structure
	 * spdk_bs_dev with a set of callbacks. These callbacks
	 * implement read, write, and other operations on the
	 * underlying disks. As a convenience, a utility function
	 * is provided that creates an spdk_bs_dev that implements
	 * all of the callbacks by forwarding the I/O to the
	 * SPDK bdev layer. Other helper functions are also
	 * available in the blob lib in blob_bdev.c that simply
	 * make it easier to layer blobstore on top of a bdev.
	 * However blobstore can be more tightly integrated into
	 * any lower layer, such as NVMe for example.
	 */
	rc = spdk_bdev_create_bs_dev_ext(hello_context->bdev_name, base_bdev_event_cb, NULL, &bs_dev);
	if (rc != 0) {
		SPDK_ERRLOG("Could not create blob bdev, %s!!\n",
			    spdk_strerror(-rc));
		spdk_app_stop(-1);
		return;
	}

	spdk_bs_init(bs_dev, NULL, bs_init_complete, hello_context);
}

int
main(int argc, char **argv)
{
	struct spdk_app_opts opts = {};
	int rc = 0;
	struct hello_context_t *hello_context = NULL;

	SPDK_NOTICELOG("entry\n");

	/* Set default values in opts structure. */
	spdk_app_opts_init(&opts, sizeof(opts));

	/*
	 * Setup a few specifics before we init, for most SPDK cmd line
	 * apps, the config file will be passed in as an arg but to make
	 * this example super simple we just hardcode it. We also need to
	 * specify a name for the app.
	 */
	opts.name = "hello_blob";
	opts.json_config_file = argv[1];


	/*
	 * Now we'll allocate and initialize the blobstore itself. We
	 * can pass in an spdk_bs_opts if we want something other than
	 * the defaults (cluster size, etc), but here we'll just take the
	 * defaults.  We'll also pass in a struct that we'll use for
	 * callbacks so we've got efficient bookkeeping of what we're
	 * creating. This is an async operation and bs_init_complete()
	 * will be called when it is complete.
	 */
	hello_context = (struct hello_context_t*)calloc(1, sizeof(struct hello_context_t));
	if (hello_context != NULL) {
		/*
		 * spdk_app_start() will block running hello_start() until
		 * spdk_app_stop() is called by someone (not simply when
		 * hello_start() returns), or if an error occurs during
		 * spdk_app_start() before hello_start() runs.
		 */
		srand(time(0));
		hello_context->channel = NULL;
		hello_context->blob_num = 0;
		hello_context->bdev_name = argv[2];
		SPDK_WARNLOG("bdev name:%s\n", hello_context->bdev_name);
		rc = spdk_app_start(&opts, hello_start, hello_context);
		if (rc) {
			SPDK_NOTICELOG("ERROR!\n");
		} else {
			SPDK_NOTICELOG("SUCCESS!\n");
		}
		/* Free up memory that we allocated */
		
	} else {
		SPDK_ERRLOG("Could not alloc hello_context struct!!\n");
		rc = -ENOMEM;
	}
	/* Gracefully close out all of the SPDK subsystems. */
	spdk_app_fini();
	hello_cleanup(hello_context);
	return rc;
}
