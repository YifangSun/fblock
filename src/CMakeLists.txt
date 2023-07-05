find_package(spdk REQUIRED)
find_package(absl REQUIRED)

add_subdirectory(blobkv)
add_subdirectory(demo)
add_subdirectory(object_manager)
add_subdirectory(spdk)
add_subdirectory(vhost)
