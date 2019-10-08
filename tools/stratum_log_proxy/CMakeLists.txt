cmake_minimum_required (VERSION 3.5)

project (btcpool_tools_stratum_log_proxy)
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
  if (ADDRESS_SANITIZER)
    set(CMAKE_CXX_FLAGS "-g -O0 -Wall -fsanitize=address -fno-omit-frame-pointer")
    message("[DEBUG] AddressSanitizer enabled, please build it with clang.")
  else()
    set(CMAKE_CXX_FLAGS "-g -O0 -Wall")
  endif()
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
find_package(OpenSSL REQUIRED)
find_package(LibEvent REQUIRED)

find_package(MYSQL REQUIRED)
# Force to use discovered OPENSSL_CRYPTO_LIBRARY & OPENSSL_SSL_LIBRARY
string(REPLACE "-lcrypto" "" MYSQL_LIBRARIES ${MYSQL_LIBRARIES})
string(REPLACE "-lssl" "" MYSQL_LIBRARIES ${MYSQL_LIBRARIES})
string(STRIP ${MYSQL_LIBRARIES} MYSQL_LIBRARIES)


###################################### Targets ######################################

include_directories(${GLOG_INCLUDE_DIRS} ${LIBCONFIGPP_INCLUDE_DIR} ${PTHREAD_INCLUDE_DIRS} ${OPENSSL_INCLUDE_DIR} ${MYSQL_INCLUDE_DIR}
                    ${LIBEVENT_INCLUDE_DIR} ${PROJECT_ROOT}/src/ssl ${PROJECT_ROOT}/3rdparty ${PROJECT_ROOT}/tools/common)

set(THIRD_LIBRARIES ${GLOG_LIBRARIES} ${LIBCONFIGPP_LIBRARY} ${LIBEVENT_LIB} ${LIBEVENT_PTHREADS_LIB} ${LIBEVENT_OPENSSL_LIB}
                    ${MYSQL_LIBRARIES} ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY} ${PTHREAD_LIBRARIES})

file(GLOB SOURCES
    ${PROJECT_ROOT}/src/ssl/*.cc
    ${PROJECT_ROOT}/tools/common/*.cpp
    src/*.hpp src/*.cc
)

add_executable(stratum_log_proxy ${SOURCES})
target_link_libraries(stratum_log_proxy ${THIRD_LIBRARIES})

###################################### Install & Package ######################################

install(TARGETS stratum_log_proxy DESTINATION bin)

# generate deb package by CPack
if(POOL__GENERATE_DEB_PACKAGE)
  set(CPACK_GENERATOR "DEB")
  set(CPACK_PACKAGE_NAME "btcpool-stratum-log-proxy")
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "YihaoPeng yihao.peng@bitmain.com")

  message("-- Package Name: ${CPACK_PACKAGE_NAME}")

  # dependencies
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

  include(CPack)
endif()
