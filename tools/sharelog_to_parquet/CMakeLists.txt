cmake_minimum_required (VERSION 3.5)

project (btcpool_tools_kafka_repeater)
set(PROJECT_ROOT "${CMAKE_SOURCE_DIR}/../..")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${PROJECT_ROOT}/cmake/Modules/")

# set C++ standard
set(CMAKE_CXX_STANDARD 14)
# stop building after the first error
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fmax-errors=1")

#
# Build Type: -DCMAKE_BUILD_TYPE=Debug|Release
# Default: Release
#
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
message("-- Build Type: ${CMAKE_BUILD_TYPE} (-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE})")
IF(CMAKE_BUILD_TYPE MATCHES Debug)
  set(CMAKE_CXX_FLAGS "-g -O0 -Wall")
ELSEIF(CMAKE_BUILD_TYPE MATCHES Release)
  set(CMAKE_CXX_FLAGS "-g -O2 -Wall")
ELSE()
  set(CMAKE_CXX_FLAGS "-g -Wall")
ENDIF()


###################################### Libs ######################################

find_package(LibConfig)
if(NOT LIBCONFIGPP_FOUND)
  message(FATAL_ERROR "LibConfig++ not found!")
endif()

find_package(Glog)
if(NOT GLOG_FOUND)
message(FATAL_ERROR "Glog not found!")
endif()

find_package(LibPthread)
if(NOT PTHREAD_FOUND)
  message(FATAL_ERROR "libpthread not found!")
endif()

find_package(ZLIB)
if(NOT ZLIB_FOUND)
  message(FATAL_ERROR "ZLib not found!")
endif()


###################################### Targets ######################################

include_directories(${PROJECT_ROOT}/3rdparty ${PROJECT_ROOT}/src ${PROJECT_ROOT}/tools/common
                    ${GLOG_INCLUDE_DIRS} ${ZLIB_INCLUDE_DIRS}
                    ${LIBCONFIGPP_INCLUDE_DIR} ${PTHREAD_INCLUDE_DIRS})

set(THIRD_LIBRARIES ${GLOG_LIBRARIES} ${LIBCONFIGPP_LIBRARY}
                    ${PTHREAD_LIBRARIES} ${ZLIB_LIBRARIES}
                    parquet arrow)

file(GLOB SOURCES *.cc)

add_executable(sharelog_to_parquet ${SOURCES})
target_link_libraries(sharelog_to_parquet ${THIRD_LIBRARIES})
