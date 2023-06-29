# tailq_test 

add_executable(tailq_test
  tailq_test.cc
)

target_link_libraries(tailq_test 
  PUBLIC
    spdk::spdk
)

target_include_directories (tailq_test
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})