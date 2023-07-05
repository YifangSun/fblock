#include <spdk/blob.h>
#include <spdk/blob_bdev.h>
#include <spdk/env.h>
#include <spdk/event.h>
#include <spdk/log.h>
#include <spdk/string.h>

#include <iostream>

#include "kvstore.h"

#define BLOB_CLUSTERS 256

struct hello_context_t {
	struct spdk_blob_store *bs;
	kvstore	kvs;

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

	free(hello_context->kvs.bf);

	/* We're all done, we can unload the blobstore. */
	unload_bs(hello_context, "", 0);
}



static void
close_file_complete(void *arg1, int bserrno) {
	struct hello_context_t *hello_context = (struct hello_context_t *)arg1;

	blob_file_delete(hello_context->bs, hello_context->kvs.bf, 
                 delete_complete, hello_context);
}

static void
close_file(struct hello_context_t *hello_context) {
    blob_file_close(hello_context->kvs.bf, close_file_complete, hello_context);
}



static void
create_file_complete(void *arg, struct blob_file* bf, int bferrno) {
	struct hello_context_t *hello_context = (struct hello_context_t *)arg;

	hello_context->kvs.bf = bf;
	SPDK_NOTICELOG("assign blob_file\n");
	close_file(hello_context);
}

static void
create_file(struct hello_context_t *hello_context) {
	blob_file_create(hello_context->bs, 67108864, create_file_complete, hello_context);
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

	create_file(hello_context);
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

	rc = spdk_bdev_create_bs_dev_ext(hello_context->bdev_name, base_bdev_event_cb, NULL, &bs_dev);
	if (rc != 0) {
		SPDK_ERRLOG("Could not create blob bdev, %s!!\n",
			    spdk_strerror(-rc));
		spdk_app_stop(-1);
		return;
	}

	spdk_bs_init(bs_dev, NULL, bs_init_complete, hello_context);
}

int main(int argc, char **argv) 
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