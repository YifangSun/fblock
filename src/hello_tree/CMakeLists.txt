# hello_blob 

add_executable(hello_tree
  hello_tree.cc
  )

target_link_libraries(hello_tree 
  PUBLIC 
    spdk::spdk
    absl::flat_hash_map
    absl::btree
  )

target_include_directories (hello_tree
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})


