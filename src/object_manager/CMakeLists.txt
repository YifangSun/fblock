add_library(object_manager STATIC
  object_manager.cc
)

target_link_libraries (object_manager
  PUBLIC
    spdk::spdk
    asan)