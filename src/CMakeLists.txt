find_package(spdk REQUIRED)
# find_package(absl REQUIRED)

add_subdirectory(blob_test)
add_subdirectory(hello_blob)
add_subdirectory(hello_tree)
add_subdirectory(spdk)
add_subdirectory(vhost)
