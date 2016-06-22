# Copyright (c) 2014 Daniel.Nachbaur@epfl.ch
#
# based on:
# https://github.com/robertmaynard/Remus/blob/master/CMake/FindZeroMQ.cmake
#
# - Try to find libzmq headers and libraries
#
# Usage of this module as follows:
#
#     find_package(libzmq)
#
# Variables used by this module, they can change the default behaviour and need
# to be set before calling find_package:
#
#  LIBZMQ_ROOT_DIR  Set this variable to the root installation of
#                            libzmq if the module has problems finding
#                            the proper installation path.
#
# Variables defined by this module:
#
#  LIBZMQ_FOUND              System has libzmq libs/headers
#  LIBZMQ_LIBRARIES          The libzmq libraries
#  LIBZMQ_INCLUDE_DIR        The location of libzmq headers
#  LIBZMQ_INCLUDE_DIRS       The location of libzmq headers

find_path(LIBZMQ_ROOT_DIR
  NAMES include/zmq.h
  HINTS $ENV{LIBZMQ_ROOT_DIR}
  )

if(MSVC)
  #add in all the names it can have on windows
  if(CMAKE_GENERATOR_TOOLSET MATCHES "v140" OR MSVC14)
    set(_zmq_TOOLSET "-v140")
  elseif(CMAKE_GENERATOR_TOOLSET MATCHES "v120" OR MSVC12)
    set(_zmq_TOOLSET "-v120")
  elseif(CMAKE_GENERATOR_TOOLSET MATCHES "v110_xp")
    set(_zmq_TOOLSET "-v110_xp")
  elseif(CMAKE_GENERATOR_TOOLSET MATCHES "v110" OR MSVC11)
    set(_zmq_TOOLSET "-v110")
  elseif(CMAKE_GENERATOR_TOOLSET MATCHES "v100" OR MSVC10)
    set(_zmq_TOOLSET "-v100")
  elseif(CMAKE_GENERATOR_TOOLSET MATCHES "v90" OR MSVC90)
    set(_zmq_TOOLSET "-v90")
  endif()

  set(_zmq_versions "4_0_5" "4_0_4" "4_0_3" "4_0_2" "4_0_1" "4_0_0"
                    "3_2_5" "3_2_4" "3_2_3" "3_2_2"  "3_2_1" "3_2_0" "3_1_0")
  set(_zmq_release_names)
  set(_zmq_debug_names)
  foreach( ver ${_zmq_versions})
    list(APPEND _zmq_release_names "libzmq${_zmq_TOOLSET}-mt-${ver}")
  endforeach()
  foreach( ver ${_zmq_versions})
    list(APPEND _zmq_debug_names "libzmq${_zmq_TOOLSET}-mt-gd-${ver}")
  endforeach()

  list(APPEND _zmq_debug_names "libzmq_d")

  set(_zmq_hint_paths ${LIBZMQ_ROOT_DIR}/bin ${LIBZMQ_ROOT_DIR}/lib)
  if(CMAKE_CL_64)
    list(APPEND _zmq_hint_paths ${LIBZMQ_ROOT_DIR}/lib/x64)
  else()
    list(APPEND _zmq_hint_paths ${LIBZMQ_ROOT_DIR}/lib/Win32)
  endif()

  #now try to find the release and debug version
  find_library(LIBZMQ_LIBRARY_RELEASE
    NAMES ${_zmq_release_names} zmq libzmq
    HINTS ${_zmq_hint_paths}
    )

  find_library(LIBZMQ_LIBRARY_DEBUG
    NAMES ${_zmq_debug_names} zmq libzmq
    HINTS ${_zmq_hint_paths}
    )

  if(LIBZMQ_LIBRARY_RELEASE AND LIBZMQ_LIBRARY_DEBUG)
    set(LIBZMQ_LIBRARY
        debug ${LIBZMQ_LIBRARY_DEBUG}
        optimized ${LIBZMQ_LIBRARY_RELEASE}
        )
  elseif(LIBZMQ_LIBRARY_RELEASE)
    set(LIBZMQLIBRARY ${LIBZMQ_LIBRARY_RELEASE})
  elseif(LIBZMQ_LIBRARY_DEBUG)
    set(LIBZMQLIBRARY ${LIBZMQ_LIBRARY_DEBUG})
  endif()

else()
  find_library(LIBZMQ_LIBRARY
    NAMES zmq libzmq
    HINTS ${LIBZMQ_ROOT_DIR}/lib
    )
endif()

find_path(LIBZMQ_INCLUDE_DIR
  NAMES zmq.h
  HINTS ${LIBZMQ_ROOT_DIR}/include
  )

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(libzmq DEFAULT_MSG
  LIBZMQ_LIBRARY
  LIBZMQ_INCLUDE_DIR
  )

set(LIBZMQ_INCLUDE_DIRS ${LIBZMQ_INCLUDE_DIR})
set(LIBZMQ_LIBRARIES ${LIBZMQ_LIBRARY})

mark_as_advanced(
  LIBZMQ_ROOT_DIR
  LIBZMQ_LIBRARY
  LIBZMQ_LIBRARY_DEBUG
  LIBZMQ_LIBRARY_RELEASE
  LIBZMQ_INCLUDE_DIR
  )
