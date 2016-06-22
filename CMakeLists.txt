cmake_minimum_required (VERSION 2.6)

project (NodeBooster)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")

#
# cmake -DCMAKE_BUILD_TYPE=Debug ..
# cmake -DCMAKE_BUILD_TYPE=Release ..
#
message("CMAKE_BUILD_TYPE = ${CMAKE_BUILD_TYPE}")
IF(CMAKE_BUILD_TYPE MATCHES Debug)
  message("Debug build.")
ELSEIF(CMAKE_BUILD_TYPE MATCHES Release)
  message("Release build.")
ELSE()
  message("Some other build type.")
ENDIF()

set(CMAKE_CXX_COMPILER "g++")
set(CMAKE_C_COMPILER "gcc")
set(CMAKE_CXX_COMPILER_ARG1 "-std=c++0x ")
set(CMAKE_CXX_FLAGS "-g -O2")

find_package(OpenSSL)
if(NOT OPENSSL_FOUND)
  message(FATAL_ERROR "OpenSSL not found!")
endif(NOT OPENSSL_FOUND)

find_package(LibConfig)
if(NOT LIBCONFIG_FOUND)
  message(FATAL_ERROR "LibConfig not found!")
endif(NOT LIBCONFIG_FOUND)

find_package(Glog)
if(NOT GLOG_FOUND)
message(FATAL_ERROR "Glog not found!")
endif(NOT GLOG_FOUND)

find_package(libzmq)
if(NOT LIBZMQ_FOUND)
  message(FATAL_ERROR "libzmq not found!")
endif(NOT LIBZMQ_FOUND)

find_package(CURL)
if(NOT CURL_FOUND)
  message(FATAL_ERROR "libcurl not found!")
endif(NOT CURL_FOUND)

find_package(Boost 1.36.0 COMPONENTS thread filesystem system regex)
if(NOT Boost_FOUND)
  message(FATAL_ERROR "Boost not found!")
endif(NOT Boost_FOUND)

include_directories(src test ${OPENSSL_INCLUDE_DIR} ${Boost_INCLUDE_DIRS} ${LIBZMQ_INCLUDE_DIR} ${GLOG_INCLUDE_DIRS})
set(THRID_LIBRARIES "-lpthread")

file(GLOB LIB_SOURCES src/*.cc src/bitcoin/*.cpp)
add_library(libnodeboost STATIC ${LIB_SOURCES})

file(GLOB_RECURSE TEST_SOURCES test/*.cc)
add_executable(unittest ${TEST_SOURCES})
target_link_libraries(unittest libnodeboost ${OPENSSL_CRYPTO_LIBRARY} ${OPENSSL_SSL_LIBRARY} ${Boost_LIBRARIES} ${LIBCONFIGPP_LIBRARY} ${LIBZMQ_LIBRARIES} ${GLOG_LIBRARIES} ${CURL_LIBRARIES} ${THRID_LIBRARIES})

file(GLOB_RECURSE NODEBOOST_SOURCES src/nodeboost/*.cc)
add_executable(nodeboost ${NODEBOOST_SOURCES})
target_link_libraries(nodeboost libnodeboost ${OPENSSL_CRYPTO_LIBRARY} ${OPENSSL_SSL_LIBRARY} ${Boost_LIBRARIES} ${LIBCONFIGPP_LIBRARY} ${LIBZMQ_LIBRARIES} ${GLOG_LIBRARIES} ${CURL_LIBRARIES} ${THRID_LIBRARIES})
