# blob_demo

add_executable(blob_demo
  blob_demo.cc
)

target_link_libraries(blob_demo 
  PUBLIC
    spdk::spdk
    asan
)

target_include_directories (blob_demo
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

#[[
set_property(TARGET blob_demo PROPERTY POSITION_INDEPENDENT_CODE ON)
if(CMAKE_BUILD_TYPE MATCHES Release)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT ltosupported OUTPUT error)
  if(ltosupported)
    set_property(TARGET blob_demo PROPERTY INTERPROCEDURAL_OPTIMIZATION ON)
  endif()
endif()
]]