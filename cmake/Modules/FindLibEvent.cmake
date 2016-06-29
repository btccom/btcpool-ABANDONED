# - Find LibEvent (a cross event library)
# This module defines
# LIBEVENT_INCLUDE_DIR, where to find LibEvent headers
# LIBEVENT_LIB, LibEvent libraries
# LIBEVENT_FOUND, If false, do not try to use libevent

find_path(
  LIBEVENT_INCLUDE_DIR
  event2/event.h
  HINTS
  ${LIBEVENT_DIR}
  $ENV{LIBEVENT_DIR}
  /usr
  /opt
  PATH_SUFFIXES include )

find_library(
  LIBEVENT_LIB
  NAMES event
  HINTS
  ${LIBEVENT_DIR}
  $ENV{LIBEVENT_DIR}
  /usr
  /opt
  PATH_SUFFIXES lib )

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibEvent DEFAULT_MSG LIBEVENT_LIB LIBEVENT_INCLUDE_DIR)
