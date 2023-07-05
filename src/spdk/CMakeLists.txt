add_library(sunyifang_spdk STATIC
  semaphore.cc
)

target_link_libraries (sunyifang_spdk
  PUBLIC
    spdk::spdk
    asan)

add_executable(semaphore_test
  semaphore_test.cc
)

target_link_libraries(semaphore_test 
  PUBLIC
    sunyifang_spdk
    asan
)