
#include "spdk/event.h"
#include "spdk/vhost.h"
#include "spdk/bdev.h"
#include "spdk/thread.h"
#include "spdk/tree.h"
#include "spdk/blob.h"

static char *g_bdev_name = "Malloc0";

struct blob_object_id {
	char *              object_name;
    struct spdk_blob *  blob;
	spdk_blob_id        blobid;   // uint64_t
	RB_ENTRY(blob_object_id) node;
};

struct hello_context_t {
	struct spdk_bdev *bdev;
	struct spdk_bdev_desc *bdev_desc;
	struct spdk_io_channel *bdev_io_channel;
	char *buff;
	char *bdev_name;
	struct spdk_bdev_io_wait_entry bdev_io_wait;
};

RB_HEAD(blob_object_tree, blob_object_id);

static int
blob_object_cmp(struct blob_object_id *blobj1, struct blob_object_id *blobj2)
{
	return strcmp(blobj1->object_name, blobj2->object_name);
}

RB_GENERATE_STATIC(blob_object_tree, blob_object_id, node, blob_object_cmp);

// 正式代码不能用static，因为还要加锁
static struct blob_object_tree g_obj_tree = RB_INITIALIZER(g_obj_tree);
static pthread_mutex_t g_obj_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_obj_tree_size = 0;

static spdk_blob_id
get_id_by_obj_name(const char *name) {
	struct blob_object_id find;
	struct blob_object_id *res;

	find.object_name = (char *)name;
	res = RB_FIND(blob_object_tree, &g_obj_tree, &find);
	if (res != NULL) {
		return res->blobid;
	}

	return -1;
}

/**
 * 进来的blob_object_id需要是已经malloc好的
 */
static int
blob_object_add(struct blob_object_id *blobj, const char *name, struct spdk_blob * blob, spdk_blob_id blobid) {
	struct spdk_bdev_name *tmp;

	blobj->object_name = strdup(name);
	if (blobj->object_name == NULL) {
		SPDK_ERRLOG("Unable to allocate blob object name\n");
		return -ENOMEM;
	}

	blobj->blob = blob;
	blobj->blobid = blobid;

	pthread_mutex_lock(&g_obj_mutex);
	tmp = (struct spdk_bdev_name *)RB_INSERT(blob_object_tree, &g_obj_tree, blobj);
	if (tmp != NULL) {
		g_obj_tree_size += 1;
	}
	pthread_mutex_unlock(&g_obj_mutex);

	if (tmp != NULL) {
		SPDK_ERRLOG("Blob object %s already exists\n", name);
		free(blobj->object_name);
		return -EEXIST;
	}

	return 0;
}

static void
blob_object_del_unsafe(struct blob_object_id *blobj) {
	RB_REMOVE(blob_object_tree, &g_obj_tree, blobj);
	g_obj_tree_size += 1;
	free(blobj->object_name);
}

static void
bdev_name_del(struct blob_object_id *blobj)
{
	pthread_mutex_lock(&g_obj_mutex);
	blob_object_del_unsafe(blobj);
	pthread_mutex_unlock(&g_obj_mutex);
}

void
rb_tree_test(void*) {
	struct blob_object_id *blobj1, *blobj2, *blobj3;
	struct blob_object_id *blobj, *tmp;
	int rc; 
	
	blobj1 = (struct blob_object_id *)calloc(1, sizeof(struct blob_object_id));
	blobj2 = (struct blob_object_id *)calloc(1, sizeof(struct blob_object_id));
	blobj3 = (struct blob_object_id *)calloc(1, sizeof(struct blob_object_id));
	
	rc = blob_object_add(blobj1, "object1", NULL, 1);
	rc = blob_object_add(blobj2, "object2", NULL, 2);
	rc = blob_object_add(blobj3, "object3", NULL, 3);

	RB_FOREACH_SAFE(blobj, blob_object_tree, &g_obj_tree, tmp) {
		if (blobj) {
			SPDK_NOTICELOG("blob object: %s id: %u\n", blobj->object_name, blobj->blobid);
		}
	}

	spdk_app_stop(0);
}


int
main(int argc, char *argv[])
{
	struct spdk_app_opts opts = {};
	int rc;
  struct hello_context_t hello_context = {};

	spdk_app_opts_init(&opts, sizeof(opts));
	opts.name = "vhost";

  if ((rc = spdk_app_parse_args(argc, argv, &opts, "b:", NULL, NULL, NULL)) 
      != SPDK_APP_PARSE_ARGS_SUCCESS) {
		exit(rc);
	}
  hello_context.bdev_name = g_bdev_name;

	/* Blocks until the application is exiting */
	rc = spdk_app_start(&opts, rb_tree_test, NULL);

  // spdk_dma_free(hello_context.buff);
	spdk_app_fini();
	return rc;
}