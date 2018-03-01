include(FindPackageHandleStandardArgs)

set(GMP_ROOT_DIR "" CACHE PATH "Folder contains libgmp")

find_path(GMP_INCLUDE_DIR gmp.h
    PATHS ${GMP_ROOT_DIR})

find_library(GMP_LIBRARY gmp
    PATHS ${GMP_ROOT_DIR}
    PATH_SUFFIXES lib lib64)

find_package_handle_standard_args(GMP DEFAULT_MSG GMP_INCLUDE_DIR GMP_LIBRARY)

if(GMP_FOUND)
  set(GMP_INCLUDE_DIRS ${GMP_INCLUDE_DIR})
  set(GMP_LIBRARIES ${GMP_LIBRARY})
  message(STATUS "Found libgmp (include: ${GMP_INCLUDE_DIR}, library: ${GMP_LIBRARY})")
  mark_as_advanced(GMP_ROOT_DIR GMP_LIBRARY GMP_INCLUDE_DIR)
endif()
