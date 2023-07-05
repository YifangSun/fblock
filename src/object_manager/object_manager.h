#include <spdk/blob.h>
#include <spdk/blob_bdev.h>
#include <spdk/env.h>
#include <spdk/event.h>
#include <spdk/log.h>
#include <spdk/string.h>

struct fb_blob {
  struct spdk_blob* blob;
  spdk_blob_id      blobid;
};

class object_manager {
public:

  absl::flat_hash_map<std::string, struct fb_blob*> table;
  struct spdk_blob_store *bs; // 我们不掌握blob_store的生命周期
};