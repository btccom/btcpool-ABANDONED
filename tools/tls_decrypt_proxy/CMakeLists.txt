cmake_minimum_required (VERSION 3.5)

project (btcpool_tools_tls_decrpyt_proxy)
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

find_package(LibConfig REQUIRED)
find_package(Glog REQUIRED)
find_package(LibPthread REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(LibEvent REQUIRED)


###################################### Targets ######################################

include_directories(${GLOG_INCLUDE_DIRS} ${LIBCONFIGPP_INCLUDE_DIR} ${PTHREAD_INCLUDE_DIRS} ${OPENSSL_INCLUDE_DIR}
                    ${LIBEVENT_INCLUDE_DIR} ${PROJECT_ROOT}/src/ssl)

set(THIRD_LIBRARIES ${GLOG_LIBRARIES} ${LIBCONFIGPP_LIBRARY} ${LIBEVENT_LIB} ${LIBEVENT_PTHREADS_LIB} ${LIBEVENT_OPENSSL_LIB}
                    ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY} ${PTHREAD_LIBRARIES})

file(GLOB SOURCES
    ${PROJECT_ROOT}/src/ssl/*.cc
    *.cc
)

add_executable(tls_decrpyt_proxy ${SOURCES})
target_link_libraries(tls_decrpyt_proxy ${THIRD_LIBRARIES})
