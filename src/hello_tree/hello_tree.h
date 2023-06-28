#include "spdk/tree.h"
#include "spdk/blob.h"

struct blob_object_id {
	char *              object_name;
    struct spdk_blob *  blob;
	spdk_blob_id        blobid;   // uint64_t
	RB_ENTRY(blob_object_id) node;
};