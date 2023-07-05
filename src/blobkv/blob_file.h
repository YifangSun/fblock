#pragma once

#include <spdk/blob.h>

struct blob_file {
public:
  bool   opened;
  size_t size;
  size_t clean;
  struct spdk_blob* blob;
  spdk_blob_id      blobid;
};

typedef void (*blob_file_create_cb)(void *arg, struct blob_file* bf, int bferrno);

typedef void (*blob_file_cb)(void *arg, int bferrno);

struct blob_file_create_ctx_t {
  struct spdk_blob_store* bs;
  struct blob_file*   bf;
  blob_file_create_cb cb_fn;
  void*               arg;
};

struct blob_file_ctx_t {
  blob_file_cb cb_fn;
  void*        arg;
};

void
blob_file_create(struct spdk_blob_store *bs, size_t size, 
                 blob_file_create_cb cb_fn, void *arg);

void
blob_file_close(struct blob_file *bf, 
                blob_file_cb cb_fn, void *arg);

void
blob_file_delete(struct spdk_blob_store *bs, struct blob_file *bf, 
                 blob_file_cb cb_fn, void *arg);



