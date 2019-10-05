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

add_definitions(-DHAVE_CONFIG_H)


###################################### Libs ######################################

find_package(LibConfig REQUIRED)
find_package(Glog REQUIRED)
find_package(LibPthread REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Protobuf REQUIRED)


# build proto files
set(PROTOBUF_FILES)
foreach(SHARE_TYPE bitcoin beam eth)
  file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/src/${SHARE_TYPE})
  execute_process(
      COMMAND ${PROTOBUF_PROTOC_EXECUTABLE} --cpp_out=${CMAKE_CURRENT_BINARY_DIR}/src/${SHARE_TYPE} ${SHARE_TYPE}.proto
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../src/${SHARE_TYPE})
  list(APPEND PROTOBUF_FILES ${CMAKE_CURRENT_BINARY_DIR}/src/${SHARE_TYPE}/${SHARE_TYPE}.pb.cc)
endforeach(SHARE_TYPE)


###################################### Targets ######################################

include_directories(. ${PROJECT_ROOT}/3rdparty ${PROJECT_ROOT}/src ${PROJECT_ROOT}/tools/common
                    ${GLOG_INCLUDE_DIRS} ${ZLIB_INCLUDE_DIRS} ${Protobuf_INCLUDE_DIRS}
                    ${LIBCONFIGPP_INCLUDE_DIR} ${PTHREAD_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR}/src)

set(THIRD_LIBRARIES ${GLOG_LIBRARIES} ${LIBCONFIGPP_LIBRARY} ${PROTOBUF_LIBRARIES}
                    ${PTHREAD_LIBRARIES} ${ZLIB_LIBRARIES}
                    parquet arrow)

file(GLOB SOURCES *.cc *.cpp ${PROJECT_ROOT}/tools/common/*.cpp)

add_executable(sharelog_to_parquet ${SOURCES} ${PROTOBUF_FILES})
target_link_libraries(sharelog_to_parquet ${THIRD_LIBRARIES})

###################################### Install & Package ######################################

install(TARGETS sharelog_to_parquet DESTINATION bin)

# generate deb package by CPack
if(POOL__GENERATE_DEB_PACKAGE)
  set(CPACK_GENERATOR "DEB")
  set(CPACK_PACKAGE_NAME "btcpool-sharelog-to-parquet")
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "YihaoPeng yihao.peng@bitmain.com")

  message("-- Package Name: ${CPACK_PACKAGE_NAME}")

  # dependencies
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

  include(CPack)
endif()
