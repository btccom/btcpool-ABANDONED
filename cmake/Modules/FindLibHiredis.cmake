include(FindPackageHandleStandardArgs)

set(Hiredis_ROOT_DIR "" CACHE PATH "Folder contains libhiredis")

find_path(Hiredis_INCLUDE_DIR hiredis/hiredis.h
    PATHS ${Hiredis_ROOT_DIR})

find_library(Hiredis_LIBRARY hiredis
    PATHS ${Hiredis_ROOT_DIR}
    PATH_SUFFIXES lib lib64)

find_package_handle_standard_args(Hiredis DEFAULT_MSG Hiredis_INCLUDE_DIR Hiredis_LIBRARY)

if(HIREDIS_FOUND)
  set(Hiredis_INCLUDE_DIRS ${Hiredis_INCLUDE_DIR})
  set(Hiredis_LIBRARIES ${Hiredis_LIBRARY})
  message(STATUS "Found libhiredis (include: ${Hiredis_INCLUDE_DIR}, library: ${Hiredis_LIBRARY})")
  mark_as_advanced(Hiredis_ROOT_DIR Hiredis_LIBRARY Hiredis_INCLUDE_DIR)
endif()
