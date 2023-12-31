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

#define MAX_BLOB_NUM 12800
#define BLOB_CLUSTERS 4
#define WRITE_UNITS 8096

/*
 * We'll use this struct to gather housekeeping hello_context to pass between
 * our events and callbacks.
 */
struct hello_context_t {
	struct spdk_blob_store *bs;
	struct spdk_blob *blob;
	spdk_blob_id blobid;

	struct spdk_blob* blobs[MAX_BLOB_NUM + 2];
	spdk_blob_id 	  blobids[MAX_BLOB_NUM + 2];

	struct spdk_blob* snapblobs[MAX_BLOB_NUM + 2];
    spdk_blob_id 	  snapids[MAX_BLOB_NUM + 2];

	struct spdk_io_channel *channel;
	char *read_buff;
	char *write_buff;
	size_t  write_units;
	uint64_t io_unit_size;
	char* bdev_name;
	int blob_num;
	int rc;
};

struct delete_snap_ctx_t {
	struct hello_context_t* hello_ctx;
	int idx;
	int max;
	uint64_t start, end;
};

struct close_snap_ctx_t {
	struct hello_context_t* hello_ctx;
	int idx;
	int max;
	uint64_t start, end;
};

struct write_snap_ctx_t {
	struct hello_context_t* hello_ctx;
	int idx;
	int max;
	uint64_t start, end;
};

struct open_snap_ctx_t {
	struct hello_context_t* hello_ctx;
	int idx;
	int max;
	uint64_t start, end;
};

struct create_snap_ctx_t {
	struct hello_context_t* hello_ctx;
	int idx;
	int max;
	uint64_t start, end;
};

struct clone_ctx_t {
	struct hello_context_t* hello_ctx;
	int idx;
	int max;
	uint64_t start, end;
};

struct write_ctx_t {
	struct hello_context_t* hello_ctx;
	int idx;
	int max;
	uint64_t start, end;
};

struct close_ctx_t {
	struct hello_context_t* hello_ctx;
	int idx;
	int max;
	uint64_t start, end;
};

struct open_ctx_t {
	struct hello_context_t* hello_ctx;
	int idx;
	int max;
	uint64_t start, end;
};

struct delete_ctx_t {
	struct hello_context_t* hello_ctx;
	int idx;
	int max;
	uint64_t start, end;
};

struct create_ctx_t {
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

/*
 * Callback routine for the deletion of a blob.
 */
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

/*
 * Function for deleting a blob.
 */
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

/*
 * Callback function for reading a blob.
 */
static void
read_complete(void *arg1, int bserrno)
{
	struct hello_context_t *hello_context = (struct hello_context_t *)arg1;
	int match_res = -1;

	SPDK_NOTICELOG("entry\n");
	if (bserrno) {
		unload_bs(hello_context, "Error in read completion",
			  bserrno);
		return;
	}

	/* Now let's make sure things match. */
	match_res = memcmp(hello_context->write_buff, hello_context->read_buff,
			   hello_context->io_unit_size);
	if (match_res) {
		unload_bs(hello_context, "Error in data compare", -1);
		return;
	} else {
		SPDK_NOTICELOG("read SUCCESS and data matches!\n");
	}

	/* Now let's close it and delete the blob in the callback. */
	spdk_blob_close(hello_context->blob, delete_blob, hello_context);
}

/*
 * Function for reading a blob.
 */
static void
read_blob(struct hello_context_t *hello_context)
{
	SPDK_NOTICELOG("entry\n");

	hello_context->read_buff = (char *)spdk_malloc(hello_context->io_unit_size,
					       0x1000, NULL, SPDK_ENV_LCORE_ID_ANY,
					       SPDK_MALLOC_DMA);
	if (hello_context->read_buff == NULL) {
		unload_bs(hello_context, "Error in memory allocation",
			  -ENOMEM);
		return;
	}

	/* Issue the read and compare the results in the callback. */
	spdk_blob_io_read(hello_context->blob, hello_context->channel,
			  hello_context->read_buff, 0, 1, read_complete,
			  hello_context);
}

/*
 * Callback function for writing a blob.
 */
static void
write_complete(void *arg1, int bserrno)
{
	struct hello_context_t *hello_context = (struct hello_context_t *)arg1;

	SPDK_NOTICELOG("entry\n");
	if (bserrno) {
		unload_bs(hello_context, "Error in write completion",
			  bserrno);
		return;
	}

	/* Now let's read back what we wrote and make sure it matches. */
	read_blob(hello_context);
}

/*
 * Function for writing to a blob.
 */
static void
blob_write(struct hello_context_t *hello_context)
{
	SPDK_NOTICELOG("entry\n");

	/*
	 * Buffers for data transfer need to be allocated via SPDK. We will
	 * transfer 1 io_unit of 4K aligned data at offset 0 in the blob.
	 */
	hello_context->write_buff = (char *)spdk_malloc(hello_context->io_unit_size,
						0x1000, NULL, SPDK_ENV_LCORE_ID_ANY,
						SPDK_MALLOC_DMA);
	if (hello_context->write_buff == NULL) {
		unload_bs(hello_context, "Error in allocating memory",
			  -ENOMEM);
		return;
	}
	memset(hello_context->write_buff, 0x5a, hello_context->io_unit_size);

	/* Now we have to allocate a channel. */
	hello_context->channel = spdk_bs_alloc_io_channel(hello_context->bs);
	if (hello_context->channel == NULL) {
		unload_bs(hello_context, "Error in allocating channel",
			  -ENOMEM);
		return;
	}

	/* Let's perform the write, 1 io_unit at offset 0. */
	spdk_blob_io_write(hello_context->blob, hello_context->channel,
			   hello_context->write_buff,
			   0, 1, write_complete, hello_context);
}

/*
 * Callback function for syncing metadata.
 */
static void
sync_complete(void *arg1, int bserrno)
{
	struct hello_context_t *hello_context = (struct hello_context_t *)arg1;

	SPDK_NOTICELOG("entry\n");
	if (bserrno) {
		unload_bs(hello_context, "Error in sync callback",
			  bserrno);
		return;
	}

	/* Blob has been created & sized & MD sync'd, let's write to it. */
	blob_write(hello_context);
}

static void
resize_complete(void *cb_arg, int bserrno)
{
	struct hello_context_t *hello_context = (struct hello_context_t *)cb_arg;
	uint64_t total = 0;

	if (bserrno) {
		unload_bs(hello_context, "Error in blob resize", bserrno);
		return;
	}

	total = spdk_blob_get_num_clusters(hello_context->blob);
	SPDK_NOTICELOG("resized blob now has USED clusters of %" PRIu64 "\n",
		       total);

	/*
	 * Metadata is stored in volatile memory for performance
	 * reasons and therefore needs to be synchronized with
	 * non-volatile storage to make it persistent. This can be
	 * done manually, as shown here, or if not it will be done
	 * automatically when the blob is closed. It is always a
	 * good idea to sync after making metadata changes unless
	 * it has an unacceptable impact on application performance.
	 */
	spdk_blob_sync_md(hello_context->blob, sync_complete, hello_context);
}

/*
 * Callback function for opening a blob.
 */
static void
open_complete(void *cb_arg, struct spdk_blob *blob, int bserrno)
{
	struct hello_context_t *hello_context = (struct hello_context_t *)cb_arg;
	uint64_t free = 0;

	SPDK_NOTICELOG("entry\n");
	if (bserrno) {
		unload_bs(hello_context, "Error in open completion",
			  bserrno);
		return;
	}


	hello_context->blob = blob;
	free = spdk_bs_free_cluster_count(hello_context->bs);
	SPDK_NOTICELOG("blobstore has FREE clusters of %" PRIu64 "\n",
		       free);

	/*
	 * Before we can use our new blob, we have to resize it
	 * as the initial size is 0. For this example we'll use the
	 * full size of the blobstore but it would be expected that
	 * there'd usually be many blobs of various sizes. The resize
	 * unit is a cluster.
	 */
	spdk_blob_resize(hello_context->blob, free, resize_complete, hello_context);
}

/*
 * Callback function for creating a blob.
 */
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

/*
 * Function for creating a blob.
 */
static void
create_blob(struct hello_context_t *hello_context)
{
	SPDK_NOTICELOG("entry\n");
	spdk_bs_create_blob(hello_context->bs, blob_create_complete, hello_context);
}

/****************************************************/
static void
delete_blob_continue(void *arg1, int bserrno) {
	struct delete_ctx_t *ctx = (struct delete_ctx_t *)arg1;
	struct hello_context_t *hello_context = ctx->hello_ctx;

	if (bserrno) {
		unload_bs(hello_context, "Error in delete completion",
			  bserrno);
		return;
	}

    // SPDK_NOTICELOG("deleted %d blob id:%" PRIu64 " \n", 
    //             ctx->idx, hello_context->blobids[ctx->idx]);

	if (ctx->idx < ctx->max) {
		ctx->idx++;
		spdk_bs_delete_blob(hello_context->bs, hello_context->blobids[ctx->idx],
			    delete_blob_continue, ctx);
	} else {
		uint64_t now = spdk_get_ticks();
		double us = env_ticks_to_usecs(now - ctx->start);
		SPDK_NOTICELOG("delete %d blobs, time: %lf\n", ctx->idx, us);

		// close_snap_iterates(hello_context);
		unload_bs(hello_context, "", 0);
		free(ctx);
		// spdk_app_stop(-1);
	}
}

static void
delete_blob_iterates(struct hello_context_t *hello_context) {
	struct delete_ctx_t *ctx = NULL;

	ctx = (struct delete_ctx_t *)calloc(1, sizeof(struct delete_ctx_t));
	ctx->hello_ctx = hello_context;
	ctx->idx = 0;
	ctx->max = MAX_BLOB_NUM;
	ctx->end = 0;
	ctx->start = spdk_get_ticks();

	spdk_bs_delete_blob(hello_context->bs, hello_context->blobids[ctx->idx],
			    delete_blob_continue, ctx);
}


static void
close_blob_continue(void *arg1, int bserrno) {
	struct close_ctx_t *ctx = (struct close_ctx_t *)arg1;
	struct hello_context_t *hello_context = ctx->hello_ctx;

	if (bserrno) {
		unload_bs(hello_context, "Error in close completion",
			  bserrno);
		return;
	}

    // SPDK_NOTICELOG("close %d blobid:%" PRIu64 " \n", 
    //         ctx->idx, hello_context->blobids[ctx->idx]);

	if (ctx->idx < ctx->max) {
		ctx->idx++;
		spdk_blob_close(hello_context->blobs[ctx->idx], close_blob_continue, ctx);
	} else {
		uint64_t now = spdk_get_ticks();
		double us = env_ticks_to_usecs(now - ctx->start);
		SPDK_NOTICELOG("close %d blobs, time: %lf\n", ctx->idx, us);

		delete_blob_iterates(hello_context);
		free(ctx);
		// spdk_app_stop(-1);
	}
}

static void
close_blob_iterates(struct hello_context_t *hello_context) {
	struct close_ctx_t *ctx = NULL;

	ctx = (struct close_ctx_t *)calloc(1, sizeof(struct close_ctx_t));
	ctx->hello_ctx = hello_context;
	ctx->idx = 0;
	ctx->max = MAX_BLOB_NUM;
	ctx->end = 0;
	ctx->start = spdk_get_ticks();

	spdk_blob_close(hello_context->blobs[ctx->idx], close_blob_continue, ctx);
}


/****************************************/
static void
delete_snap_continue(void *arg1, int bserrno) {
	struct delete_snap_ctx_t *ctx = (struct delete_snap_ctx_t *)arg1;
	struct hello_context_t *hello_context = ctx->hello_ctx;

	if (bserrno) {
		unload_bs(hello_context, "Error in delete snapshot completion",
			  bserrno);
		return;
	}

    // SPDK_NOTICELOG("deleted %d snap id:%" PRIu64 " \n", 
    //             ctx->idx, hello_context->snapids[ctx->idx]);

	if (ctx->idx < ctx->max) {
		ctx->idx++;
		spdk_bs_delete_blob(hello_context->bs, hello_context->snapids[ctx->idx],
			    delete_snap_continue, ctx);
	} else {
		uint64_t now = spdk_get_ticks();
		double us = env_ticks_to_usecs(now - ctx->start);
		SPDK_NOTICELOG("delete %d snapshots, time: %lf\n", ctx->idx, us);

		// unload_bs(hello_context, "", 0);
		close_blob_iterates(hello_context);
		free(ctx);
		// spdk_app_stop(-1);
	}
}

static void
delete_snap_iterates(struct hello_context_t *hello_context) {
	struct delete_snap_ctx_t *ctx = NULL;

	ctx = (struct delete_snap_ctx_t *)calloc(1, sizeof(struct delete_snap_ctx_t));
	ctx->hello_ctx = hello_context;
	ctx->idx = 0;
	ctx->max = MAX_BLOB_NUM;
	ctx->end = 0;
	ctx->start = spdk_get_ticks();

	spdk_bs_delete_blob(hello_context->bs, hello_context->snapids[ctx->idx],
			    delete_snap_continue, ctx);
}

static void
close_snap_continue(void *arg1, int bserrno) {
	struct close_snap_ctx_t *ctx = (struct close_snap_ctx_t *)arg1;
	struct hello_context_t *hello_context = ctx->hello_ctx;

	if (bserrno) {
		unload_bs(hello_context, "Error in close snapshot completion",
			  bserrno);
		return;
	}

    // SPDK_NOTICELOG("close %d snap id:%" PRIu64 " \n", 
    //         ctx->idx, hello_context->snapids[ctx->idx]);

	if (ctx->idx < ctx->max) {
		ctx->idx++;
		spdk_blob_close(hello_context->snapblobs[ctx->idx], close_snap_continue, ctx);
	} else {
		uint64_t now = spdk_get_ticks();
		double us = env_ticks_to_usecs(now - ctx->start);
		SPDK_NOTICELOG("close %d snapshots, time: %lf\n", ctx->idx, us);

		delete_snap_iterates(hello_context);
		free(ctx);
		// spdk_app_stop(-1);
	}
}

static void
close_snap_iterates(struct hello_context_t *hello_context) {
	struct close_snap_ctx_t *ctx = NULL;

	ctx = (struct close_snap_ctx_t *)calloc(1, sizeof(struct close_snap_ctx_t));
	ctx->hello_ctx = hello_context;
	ctx->idx = 0;
	ctx->max = MAX_BLOB_NUM;
	ctx->end = 0;
	ctx->start = spdk_get_ticks();

	spdk_blob_close(hello_context->snapblobs[ctx->idx], close_snap_continue, ctx);
}

/***********************************/

static void
rewrite_blob_continue(void *arg1, int bserrno) {
	struct write_ctx_t *ctx = (struct write_ctx_t *)arg1;
	struct hello_context_t *hello_context = ctx->hello_ctx;

	if (bserrno) {
		unload_bs(hello_context, "Error in rewrite blob completion",
			  bserrno);
		return;
	}

    // SPDK_NOTICELOG("writed %d blob id:%" PRIu64 " \n", 
    //         ctx->idx, hello_context->blobids[ctx->idx]);

	if (ctx->idx < ctx->max) {
		ctx->idx++;

		spdk_blob_io_write(hello_context->blobs[ctx->idx], hello_context->channel,
			   hello_context->write_buff,
			   0, 2, rewrite_blob_continue, ctx);
	} else {
		uint64_t now = spdk_get_ticks();
		double us = env_ticks_to_usecs(now - ctx->start);
		SPDK_NOTICELOG("rewrite %d blob, time: %lf\n", ctx->idx, us);

		close_snap_iterates(hello_context);
		free(ctx);
		// spdk_app_stop(-1);
	}
}

static void
rewrite_blob_iterates(struct hello_context_t *hello_context) {
	struct write_ctx_t *ctx = NULL;

	ctx = (struct write_ctx_t *)calloc(1, sizeof(struct write_ctx_t));
	ctx->hello_ctx = hello_context;
	ctx->idx = 0;
	ctx->max = MAX_BLOB_NUM;
	ctx->end = 0;
	ctx->start = spdk_get_ticks();

	spdk_blob_io_write(hello_context->blobs[ctx->idx], hello_context->channel,
			   hello_context->write_buff,
			   0, 2, rewrite_blob_continue, ctx);
}


static void
open_snap_continue(void *cb_arg, struct spdk_blob *blob, int bserrno) {
	struct open_snap_ctx_t *ctx = (struct open_snap_ctx_t *)cb_arg;
	struct hello_context_t *hello_context = ctx->hello_ctx;

	if (bserrno) {
		unload_bs(hello_context, "Error in open snapshot completion",
			  bserrno);
		return;
	}

	hello_context->snapblobs[ctx->idx] = blob;
    // SPDK_NOTICELOG("opened %d snapshot id:%" PRIu64 " \n", 
    //         ctx->idx, hello_context->snapids[ctx->idx]);

	if (ctx->idx < ctx->max) {
		ctx->idx++;
		spdk_bs_open_blob(hello_context->bs, hello_context->snapids[ctx->idx],
			  open_snap_continue, ctx);
	} else {
		uint64_t now = spdk_get_ticks();
		double us = env_ticks_to_usecs(now - ctx->start);
		SPDK_NOTICELOG("opened %d snapshots, time: %lf\n", ctx->idx, us);

		rewrite_blob_iterates(hello_context);
		free(ctx);
		// spdk_app_stop(-1);
	}
}

static void
open_snap_iterates(struct hello_context_t *hello_context) {
	struct open_snap_ctx_t *ctx = NULL;

	ctx = (struct open_snap_ctx_t *)calloc(1, sizeof(struct open_snap_ctx_t));
	ctx->hello_ctx = hello_context;
	ctx->idx = 0;
	ctx->max = MAX_BLOB_NUM;
	ctx->end = 0;
	ctx->start = spdk_get_ticks();

	spdk_bs_open_blob(hello_context->bs, hello_context->snapids[ctx->idx],
			  open_snap_continue, ctx);
}


static void
create_snap_continue(void *arg1, spdk_blob_id blobid, int bserrno) {
	struct create_snap_ctx_t *ctx = (struct create_snap_ctx_t *)arg1;
	struct hello_context_t *hello_context = ctx->hello_ctx;

	if (bserrno) {
		unload_bs(hello_context, "Error in blob create snapshot callback",
				bserrno);
		return;
	}

	hello_context->snapids[ctx->idx] = blobid;
    // SPDK_NOTICELOG("created snapshot %d blob id:%" PRIu64 "\n", ctx->idx, 
    //         hello_context->snaps[ctx->idx]);

	if (ctx->idx < ctx->max) {
		ctx->idx++;

		spdk_bs_create_snapshot(hello_context->bs, hello_context->blobids[ctx->idx],
			     NULL, create_snap_continue, ctx);
	} else {
		uint64_t now = spdk_get_ticks();
		double us = env_ticks_to_usecs(now - ctx->start);
		SPDK_NOTICELOG("create %d snapshots, time: %lf\n", ctx->idx, us);

		open_snap_iterates(hello_context);
		free(ctx);
		// spdk_app_stop(-1);
	}
}

static void
create_snap_iterates(struct hello_context_t *hello_context) {
	struct create_snap_ctx_t *ctx = NULL;

	ctx = (struct create_snap_ctx_t*)calloc(1, sizeof(struct create_snap_ctx_t));
	ctx->hello_ctx = hello_context;
	ctx->idx = 0;
	ctx->max = MAX_BLOB_NUM;
	ctx->end = 0;
	ctx->start = spdk_get_ticks();

	spdk_bs_create_snapshot(hello_context->bs, hello_context->blobids[ctx->idx],
             NULL, create_snap_continue, ctx);
}


/*****************************************************/

static void
write_blob_continue(void *arg1, int bserrno) {
	struct write_ctx_t *ctx = (struct write_ctx_t *)arg1;
	struct hello_context_t *hello_context = ctx->hello_ctx;

	if (bserrno) {
		unload_bs(hello_context, "Error in write blob completion",
			  bserrno);
		return;
	}

    // SPDK_NOTICELOG("writed %d blob id:%" PRIu64 " \n", 
    //         ctx->idx, hello_context->blobids[ctx->idx]);

	if (ctx->idx < ctx->max) {
		ctx->idx++;

		spdk_blob_io_write(hello_context->blobs[ctx->idx], hello_context->channel,
			   hello_context->write_buff,
			   0, 2, write_blob_continue, ctx);
	} else {
		uint64_t now = spdk_get_ticks();
		double us = env_ticks_to_usecs(now - ctx->start);
		SPDK_NOTICELOG("write %d blob, time: %lf\n", ctx->idx, us);

		create_snap_iterates(hello_context);
		free(ctx);
		// spdk_app_stop(-1);
	}
}

static void
write_blob_iterates(struct hello_context_t *hello_context) {
	struct write_ctx_t *ctx = NULL;

	ctx = (struct write_ctx_t*)calloc(1, sizeof(struct write_ctx_t));
	ctx->hello_ctx = hello_context;
	ctx->idx = 0;
	ctx->max = MAX_BLOB_NUM;
	ctx->end = 0;
	ctx->start = spdk_get_ticks();
    hello_context->channel = spdk_bs_alloc_io_channel(hello_context->bs);
    hello_context->write_buff = (char*)spdk_malloc(512 * WRITE_UNITS,
                    0x1000, NULL, SPDK_ENV_LCORE_ID_ANY,
                    SPDK_MALLOC_DMA);
    memset(hello_context->write_buff, 0x5a, 512 * WRITE_UNITS);

	spdk_blob_io_write(hello_context->blobs[ctx->idx], hello_context->channel,
			   hello_context->write_buff,
			   0, 2, write_blob_continue, ctx);
}


static void
open_blob_continue(void *cb_arg, struct spdk_blob *blob, int bserrno) {
	struct open_ctx_t *ctx = (struct open_ctx_t *)cb_arg;
	struct hello_context_t *hello_context = ctx->hello_ctx;

	if (bserrno) {
		unload_bs(hello_context, "Error in open completion",
			  bserrno);
		return;
	}

	hello_context->blobs[ctx->idx] = blob;
    // SPDK_NOTICELOG("opened %d blob id:%" PRIu64 " \n", 
    //         ctx->idx, hello_context->blobids[ctx->idx]);

	if (ctx->idx < ctx->max) {
		ctx->idx++;
		spdk_bs_open_blob(hello_context->bs, hello_context->blobids[ctx->idx],
			  open_blob_continue, ctx);
	} else {
		uint64_t now = spdk_get_ticks();
		double us = env_ticks_to_usecs(now - ctx->start);
		SPDK_NOTICELOG("opened %d blob, time: %lf\n", ctx->idx, us);

		write_blob_iterates(hello_context);
		free(ctx);
		// spdk_app_stop(-1);
	}
}

static void
open_blob_iterates(struct hello_context_t *hello_context) {
	struct open_ctx_t *ctx = NULL;

	ctx = (struct open_ctx_t *)calloc(1, sizeof(struct open_ctx_t));
	ctx->hello_ctx = hello_context;
	ctx->idx = 0;
	ctx->max = MAX_BLOB_NUM;
	ctx->end = 0;
	ctx->start = spdk_get_ticks();

	spdk_bs_open_blob(hello_context->bs, hello_context->blobids[ctx->idx],
			  open_blob_continue, ctx);
}


static void
create_blob_continue(void *arg1, spdk_blob_id blobid, int bserrno) {
	struct create_ctx_t *ctx = (struct create_ctx_t *)arg1;
	struct hello_context_t *hello_context = ctx->hello_ctx;

	if (bserrno) {
		unload_bs(hello_context, "Error in blob create callback",
				bserrno);
		return;
	}

	hello_context->blobids[ctx->idx] = blobid;
    // SPDK_NOTICELOG("created %d blob id:%" PRIu64 "\n", ctx->idx, 
    //         hello_context->blobids[ctx->idx]);

	if (ctx->idx < ctx->max) {
		ctx->idx++;

		struct spdk_blob_opts opts;
		spdk_blob_opts_init(&opts, sizeof(opts));
		opts.num_clusters = BLOB_CLUSTERS;
		spdk_bs_create_blob_ext(hello_context->bs, &opts, create_blob_continue, ctx);
	} else {
		uint64_t now = spdk_get_ticks();
		double us = env_ticks_to_usecs(now - ctx->start);
		SPDK_NOTICELOG("created %d blob, time: %lf\n", ctx->idx, us);

		open_blob_iterates(hello_context);
		free(ctx);
		// spdk_app_stop(-1);
	}
}

static void
create_blob_iterates(struct hello_context_t *hello_context) {
	struct create_ctx_t *ctx = NULL;

	ctx = (struct create_ctx_t *)calloc(1, sizeof(struct create_ctx_t));
	ctx->hello_ctx = hello_context;
	ctx->idx = 0;
	ctx->max = MAX_BLOB_NUM;
	ctx->end = 0;
	ctx->start = spdk_get_ticks();

	struct spdk_blob_opts opts;
	spdk_blob_opts_init(&opts, sizeof(opts));
	opts.num_clusters = BLOB_CLUSTERS;
	spdk_bs_create_blob_ext(hello_context->bs, &opts, create_blob_continue, ctx);
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

	SPDK_NOTICELOG("entry\n");
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

	create_blob_iterates(hello_context);
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
