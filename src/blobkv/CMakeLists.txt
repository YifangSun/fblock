# hello_blob 

add_executable(blobkv
  blob_file.cc
  blobkv.cc
  coding.cc
  kvstore.cc
)

add_compile_options(
  -Wno-error=pointer-arith  # 对void*进行加减运算
  -Wno-error=write-strings  # 把常量字符串当char*使用
  -Wno-error=format   
)

target_link_libraries(blobkv 
  PUBLIC 
    spdk::spdk
    asan
    absl::flat_hash_map
    absl::btree
)

target_include_directories (blobkv
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})