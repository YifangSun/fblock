#include <spdk/blob.h>
#include <spdk/blob_bdev.h>
#include <spdk/env.h>
#include <spdk/event.h>
#include <spdk/log.h>
#include <spdk/string.h>
#include <spdk/util.h>

#include "blob_file.h"

/**
 * 删除 blob，正常流程不会调用。
 * 需要管理员从外部调用，因为要传入 spdk_blob_store 指针。
 */
static void
_blob_delete_cb(void *arg1, int bserrno)
{
  struct blob_file_ctx_t* ctx = (struct blob_file_ctx_t *)arg1;

	ctx->cb_fn(ctx->arg, 0);
  free(ctx);
}

void
blob_file_delete(struct spdk_blob_store *bs, struct blob_file *bf, 
                 blob_file_cb cb_fn, void *arg)
{
  struct blob_file_ctx_t* ctx;

  ctx = (struct blob_file_ctx_t*)malloc(sizeof(struct blob_file_ctx_t));
  ctx->cb_fn = cb_fn;
  ctx->arg = arg;
  
	spdk_bs_delete_blob(bs, bf->blobid, _blob_delete_cb, ctx);
}


/**
 * 只负责 close blob，不删除。因为一般情况我们希望blob一直保留在磁盘上。
 * 只需要传入 blob_file 指针即可。
 */
static void
_blob_close_cb(void *arg1, int bserrno)
{
  struct blob_file_ctx_t* ctx = (struct blob_file_ctx_t *)arg1;

  if (bserrno) {
		SPDK_ERRLOG("Could not close blob, %s!!\n", spdk_strerror(-bserrno));
		return;
	}

	ctx->cb_fn(ctx->arg, 0);
  free(ctx);
}

void
blob_file_close(struct blob_file *bf, blob_file_cb cb_fn, void *arg) {
  struct blob_file_ctx_t* ctx;

  ctx = (struct blob_file_ctx_t*)malloc(sizeof(struct blob_file_ctx_t));
  ctx->cb_fn = cb_fn;
  ctx->arg = arg;
  
  spdk_blob_close(bf->blob, _blob_close_cb, ctx);
}


/**
 * 一次性创建blob并open
 */
static void
_blob_open_cb(void *arg1, struct spdk_blob *blob, int bserrno)
{
	struct blob_file_create_ctx_t* ctx = (struct blob_file_create_ctx_t *)arg1;
  SPDK_NOTICELOG("lib blob_open_cb, %s!!\n", spdk_strerror(bserrno));

	if (bserrno) {
		SPDK_ERRLOG("Could not open blob, %s!!\n", spdk_strerror(bserrno));
		return;
	}

	ctx->bf->blob = blob;
  ctx->bf->opened = true;
  SPDK_NOTICELOG("lib blob_open_cb, run cb\n");
	ctx->cb_fn(ctx->arg, ctx->bf, 0);
  SPDK_NOTICELOG("lib blob_open_cb, after cb\n");
  free(ctx);
}

static void
_blob_create_cb(void *arg1, spdk_blob_id blobid, int bserrno)
{
  struct blob_file_create_ctx_t* ctx = (struct blob_file_create_ctx_t *)arg1;
  SPDK_NOTICELOG("lib _blob_create_cb, %s!!\n", spdk_strerror(bserrno));

	if (bserrno) {
		SPDK_ERRLOG("Could not create blob, %s!!\n", spdk_strerror(bserrno));
		return;
	}

	ctx->bf->blobid = blobid;

	spdk_bs_open_blob(ctx->bs, blobid, _blob_open_cb, ctx);
  SPDK_NOTICELOG("lib _blob_create_cb, after spdk_bs_open_blob\n");
}

void
blob_file_create(struct spdk_blob_store *bs, size_t size, 
                 blob_file_create_cb cb_fn, void *arg)
{
  /// TODO: blob_file 最好用 lw_shared_ptr 管理起来
  struct blob_file* bf;
  struct blob_file_create_ctx_t* ctx;
  struct spdk_blob_opts opts;
  uint64_t num_clusters;

  bf = (struct blob_file*)malloc(sizeof(struct blob_file));
  ctx = (struct blob_file_create_ctx_t*)malloc(sizeof(struct blob_file_create_ctx_t));
  num_clusters = spdk_divide_round_up(size, spdk_bs_get_cluster_size(bs));
  ctx->bs = bs;
  ctx->bf = bf;
  ctx->cb_fn = cb_fn;
  ctx->arg = arg;
  
  spdk_blob_opts_init(&opts, sizeof(opts));
  opts.num_clusters = num_clusters;
  spdk_bs_create_blob_ext(bs, &opts, _blob_create_cb, ctx);
}
