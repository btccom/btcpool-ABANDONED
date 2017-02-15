include(FindPackageHandleStandardArgs)

set(ZOOKEEPER_ROOT_DIR "" CACHE PATH "Folder contains Zookeeper C Client")

find_path(ZOOKEEPER_INCLUDE_DIR zookeeper/zookeeper.h
    PATHS ${ZOOKEEPER_ROOT_DIR})

find_library(ZOOKEEPER_LIBRARY zookeeper_mt
    PATHS ${ZOOKEEPER_ROOT_DIR}
    PATH_SUFFIXES lib lib64)

find_package_handle_standard_args(Zookeeper DEFAULT_MSG ZOOKEEPER_INCLUDE_DIR ZOOKEEPER_LIBRARY)

if(ZOOKEEPER_FOUND)
  set(ZOOKEEPER_INCLUDE_DIRS ${ZOOKEEPER_INCLUDE_DIR})
  set(ZOOKEEPER_LIBRARIES ${ZOOKEEPER_LIBRARY})
  message(STATUS "Found Zookeeper (include: ${ZOOKEEPER_INCLUDE_DIR}, library: ${ZOOKEEPER_LIBRARY})")
  mark_as_advanced(ZOOKEEPER_ROOT_DIR ZOOKEEPER_LIBRARY ZOOKEEPER_INCLUDE_DIR)
endif()
