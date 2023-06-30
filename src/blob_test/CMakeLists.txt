# blob_test 

add_executable(blob_test
  blob_test.cc
)

target_link_libraries(blob_test 
  PUBLIC
    spdk::spdk
    asan
)

target_include_directories (blob_test
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

#[[
set_property(TARGET blob_test PROPERTY POSITION_INDEPENDENT_CODE ON)
if(CMAKE_BUILD_TYPE MATCHES Release)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT ltosupported OUTPUT error)
  if(ltosupported)
    set_property(TARGET blob_test PROPERTY INTERPROCEDURAL_OPTIMIZATION ON)
  endif()
endif()
]]