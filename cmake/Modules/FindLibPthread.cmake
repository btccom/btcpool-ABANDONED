include(FindPackageHandleStandardArgs)

set(PTHREAD_ROOT_DIR "" CACHE PATH "Folder contains libpthread")

find_path(PTHREAD_INCLUDE_DIR pthread.h
    PATHS ${PTHREAD_ROOT_DIR})

find_library(PTHREAD_LIBRARY pthread
    PATHS ${PTHREAD_ROOT_DIR}
    PATH_SUFFIXES lib lib64)

find_package_handle_standard_args(PTHREAD DEFAULT_MSG PTHREAD_INCLUDE_DIR PTHREAD_LIBRARY)

if(PTHREAD_FOUND)
  set(PTHREAD_INCLUDE_DIRS ${PTHREAD_INCLUDE_DIR})
  set(PTHREAD_LIBRARIES ${PTHREAD_LIBRARY})
  message(STATUS "Found libpthread (include: ${PTHREAD_INCLUDE_DIR}, library: ${PTHREAD_LIBRARY})")
  mark_as_advanced(PTHREAD_ROOT_DIR PTHREAD_LIBRARY PTHREAD_INCLUDE_DIR)
endif()
