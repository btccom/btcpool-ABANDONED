cmake_minimum_required (VERSION 3.5)

project (btcpool_tools_worker_update)
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

find_package(KafkaC)
if(NOT KAFKA_FOUND)
  message(FATAL_ERROR "librdkafka not found!")
endif()

find_package(LibPthread)
if(NOT PTHREAD_FOUND)
  message(FATAL_ERROR "libpthread not found!")
endif()

find_package(Boost 1.36.0 COMPONENTS thread filesystem system regex program_options)
if(NOT Boost_FOUND)
  message(FATAL_ERROR "Boost not found!")
endif()

find_package(OpenSSL)
if(NOT OPENSSL_FOUND)
  message(FATAL_ERROR "OpenSSL not found!")
endif()

find_package(ZLIB)
if(NOT ZLIB_FOUND)
  message(FATAL_ERROR "ZLib not found!")
endif()

find_package(MYSQL)
if(NOT MYSQL_FOUND)
  message(FATAL_ERROR "libmysqlclient not found!")
endif()
# Force to use discovered OPENSSL_CRYPTO_LIBRARY & OPENSSL_SSL_LIBRARY
string(REPLACE "-lcrypto" "" MYSQL_LIBRARIES ${MYSQL_LIBRARIES})
string(REPLACE "-lssl" "" MYSQL_LIBRARIES ${MYSQL_LIBRARIES})
string(STRIP ${MYSQL_LIBRARIES} MYSQL_LIBRARIES)

find_package(Sasl2)


###################################### Targets ######################################

include_directories(${PROJECT_ROOT}/3rdparty ${GLOG_INCLUDE_DIRS} ${KAFKA_INCLUDE_DIRS} ${Boost_INCLUDE_DIRS} ${ZLIB_INCLUDE_DIRS}
                    ${MYSQL_INCLUDE_DIR} ${LIBCONFIGPP_INCLUDE_DIR} ${PTHREAD_INCLUDE_DIRS} ${SASL2_INCLUDE_DIR}
                    ${OPENSSL_INCLUDE_DIR} ${PROJECT_ROOT}/src ${PROJECT_ROOT}/tools/common)

set(THIRD_LIBRARIES ${GLOG_LIBRARIES} ${KAFKA_LIBRARIES} ${LIBCONFIGPP_LIBRARY} ${Boost_LIBRARIES} ${MYSQL_LIBRARIES}
                    ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY} ${PTHREAD_LIBRARIES} ${ZLIB_LIBRARIES} ${SASL2_LIBRARIES} -ldl)

file(GLOB SOURCES
    ${PROJECT_ROOT}/src/Kafka.cc
    *.cc
)

add_executable(worker_update ${SOURCES})
target_link_libraries(worker_update ${THIRD_LIBRARIES})

###################################### Install & Package ######################################

install(TARGETS worker_update DESTINATION bin)

# generate deb package by CPack
if(POOL__GENERATE_DEB_PACKAGE)
  set(CPACK_GENERATOR "DEB")
  set(CPACK_PACKAGE_NAME "btcpool-worker-update")
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "YihaoPeng yihao.peng@bitmain.com")

  message("-- Package Name: ${CPACK_PACKAGE_NAME}")

  # dependencies
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

  include(CPack)
endif()
