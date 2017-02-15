cmake_minimum_required (VERSION 2.6)

project (BTCPOOL)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")

#
# cmake -DCMAKE_BUILD_TYPE=Debug ..
# cmake -DCMAKE_BUILD_TYPE=Release ..
#
message("CMAKE_BUILD_TYPE = ${CMAKE_BUILD_TYPE}")
IF(CMAKE_BUILD_TYPE MATCHES Debug)
  message("Debug build.")
  set(CMAKE_CXX_FLAGS "-g -O0 -Wall")
ELSEIF(CMAKE_BUILD_TYPE MATCHES Release)
  message("Release build.")
  set(CMAKE_CXX_FLAGS "-O2 -Wall")
ELSE()
  message("Some other build type.")
ENDIF()

set(CMAKE_CXX_COMPILER "g++")
set(CMAKE_C_COMPILER "gcc")
set(CMAKE_CXX_COMPILER_ARG1 "-std=c++0x ")

set(secp256k1_DIR "${CMAKE_CURRENT_BINARY_DIR}/../src/bitcoin/secp256k1")
set(secp256k1_LIBRARIES "${secp256k1_DIR}/.libs/libsecp256k1.a")
if (NOT EXISTS ${secp256k1_LIBRARIES})
  message(STATUS "build secp256k1...")
  execute_process(WORKING_DIRECTORY "${secp256k1_DIR}" COMMAND ./autogen.sh)
  execute_process(WORKING_DIRECTORY "${secp256k1_DIR}" COMMAND ./configure)
  execute_process(WORKING_DIRECTORY "${secp256k1_DIR}" COMMAND make)
endif()

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

find_package(LibEvent)
if(NOT LibEvent_FOUND)
  message(FATAL_ERROR "libevent2 not found!")
endif(NOT LibEvent_FOUND)

find_package(Boost 1.36.0 COMPONENTS thread filesystem system regex program_options)
if(NOT Boost_FOUND)
  message(FATAL_ERROR "Boost not found!")
endif(NOT Boost_FOUND)

find_package(ZookeeperC)
if(NOT ZOOKEEPER_FOUND)
  message(FATAL_ERROR "libzookeeper-mt not found!")
endif(NOT ZOOKEEPER_FOUND)

find_package(KafkaC)
if(NOT KAFKA_FOUND)
  message(FATAL_ERROR "librdkafka not found!")
endif(NOT KAFKA_FOUND)

execute_process(COMMAND mysql_config --libs_r OUTPUT_VARIABLE MYSQL_LIB OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND mysql_config --include OUTPUT_VARIABLE MYSQL_INCLUDE OUTPUT_STRIP_TRAILING_WHITESPACE)

include_directories(src test src/bitcoin src/bitcoin/secp256k1/include ${OPENSSL_INCLUDE_DIR} ${Boost_INCLUDE_DIRS} ${LIBZMQ_INCLUDE_DIR} ${GLOG_INCLUDE_DIRS} ${LIBEVENT_INCLUDE_DIR} ${MYSQL_INCLUDE})
set(THIRD_LIBRARIES ${OPENSSL_CRYPTO_LIBRARY} ${OPENSSL_SSL_LIBRARY} ${Boost_LIBRARIES} ${LIBCONFIGPP_LIBRARY} ${LIBZMQ_LIBRARIES} ${GLOG_LIBRARIES} ${CURL_LIBRARIES} ${ZOOKEEPER_LIBRARIES} ${KAFKA_LIBRARIES} ${LIBEVENT_LIB} ${LIBEVENT_PTHREADS_LIB} ${secp256k1_LIBRARIES} pthread mysqlclient gmp)

file(GLOB LIB_SOURCES src/*.cc src/bitcoin/*.cpp src/bitcoin/consensus/*.cpp src/bitcoin/crypto/*.cpp src/bitcoin/primitives/*.cpp src/bitcoin/script/*.cpp src/bitcoin/support/*.cpp)
add_library(btcpool STATIC ${LIB_SOURCES})

file(GLOB_RECURSE TEST_SOURCES test/*.cc)
add_executable(unittest ${TEST_SOURCES})
target_link_libraries(unittest btcpool ${THIRD_LIBRARIES})

file(GLOB_RECURSE GBTMAKER_SOURCES src/gbtmaker/*.cc)
add_executable(gbtmaker ${GBTMAKER_SOURCES})
target_link_libraries(gbtmaker btcpool ${THIRD_LIBRARIES})

file(GLOB_RECURSE NMC_AUXBLOCK_MAKER_SOURCES src/nmcauxmaker/*.cc)
add_executable(nmcauxmaker ${NMC_AUXBLOCK_MAKER_SOURCES})
target_link_libraries(nmcauxmaker btcpool ${THIRD_LIBRARIES})

file(GLOB_RECURSE JOBMAKER_SOURCES src/jobmaker/*.cc)
add_executable(jobmaker ${JOBMAKER_SOURCES})
target_link_libraries(jobmaker btcpool ${THIRD_LIBRARIES})

file(GLOB_RECURSE SSERVER_SOURCES src/sserver/*.cc)
add_executable(sserver ${SSERVER_SOURCES})
target_link_libraries(sserver btcpool ${THIRD_LIBRARIES})

file(GLOB_RECURSE STATSHTTPD_SOURCES src/statshttpd/*.cc)
add_executable(statshttpd ${STATSHTTPD_SOURCES})
target_link_libraries(statshttpd btcpool ${THIRD_LIBRARIES})

file(GLOB_RECURSE SHARELOGGER_SOURCES src/sharelogger/*.cc)
add_executable(sharelogger ${SHARELOGGER_SOURCES})
target_link_libraries(sharelogger btcpool ${THIRD_LIBRARIES})

file(GLOB_RECURSE SLPARSER_SOURCES src/slparser/*.cc)
add_executable(slparser ${SLPARSER_SOURCES})
target_link_libraries(slparser btcpool ${THIRD_LIBRARIES})

file(GLOB_RECURSE BLKMAKER_SOURCES src/blkmaker/*.cc)
add_executable(blkmaker ${BLKMAKER_SOURCES})
target_link_libraries(blkmaker btcpool ${THIRD_LIBRARIES})

file(GLOB_RECURSE SIMULATOR_SOURCES src/simulator/*.cc)
add_executable(simulator ${SIMULATOR_SOURCES})
target_link_libraries(simulator btcpool ${THIRD_LIBRARIES})

file(GLOB_RECURSE POOLWATCHER_SOURCES src/poolwatcher/*.cc)
add_executable(poolwatcher ${POOLWATCHER_SOURCES})
target_link_libraries(poolwatcher btcpool ${THIRD_LIBRARIES})
