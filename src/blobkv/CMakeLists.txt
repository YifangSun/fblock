# hello_blob 

add_executable(blobkv
  blobkv.cc
  )

target_link_libraries(blobkv 
  PUBLIC 
    spdk::spdk
    asan
    # absl::flat_hash_map
    # absl::btree
  )

target_include_directories (blobkv
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})