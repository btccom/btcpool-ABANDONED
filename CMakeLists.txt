cmake_minimum_required (VERSION 2.6)

project (BTCPOOL)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")

#
# cmake -DCMAKE_BUILD_TYPE=Debug ..
# cmake -DCMAKE_BUILD_TYPE=Release ..
# 
# default: Release
#

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

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

# The number of concurrent jobs when compiling a third-party library
if(JOBS)
  message("-- concurrent jobs: -DJOBS=${JOBS}")
  set(MAKE_JOBS "-j${JOBS}") 
else()
  message("-- concurrent jobs: -DJOBS=1")
  set(MAKE_JOBS "") 
endif()

# add a macro that needed by codes copied from Bitcoin Core
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DHAVE_CONFIG_H")


set(CMAKE_CXX_COMPILER "g++")
set(CMAKE_C_COMPILER "gcc")
set(CMAKE_CXX_COMPILER_ARG1 "-std=c++0x ")


#
# Bitcoin Core Root
#
message("-- Bitcoin Core source code root directory: -DBITCOIN_SRC_ROOT=${BITCOIN_SRC_ROOT}")
if (NOT EXISTS "${BITCOIN_SRC_ROOT}/src/version.h")
  message(FATAL_ERROR "The source code of Bitcoin Core not exists!")
endif()

#
# Bitcoin Core libs
#
set(BITCOIN_LIBRARIES
  "${BITCOIN_SRC_ROOT}/src/libbitcoin_common.a"
  "${BITCOIN_SRC_ROOT}/src/libbitcoin_consensus.a"
  "${BITCOIN_SRC_ROOT}/src/libbitcoin_util.a"
  "${BITCOIN_SRC_ROOT}/src/crypto/libbitcoin_crypto.a")

list(GET BITCOIN_LIBRARIES 0 BITCOIN_LIBRARIE)
if (NOT EXISTS ${BITCOIN_LIBRARIE})
  message(STATUS "building Bitcoin Core...")
  execute_process(WORKING_DIRECTORY "${BITCOIN_SRC_ROOT}" COMMAND ./autogen.sh)
  execute_process(WORKING_DIRECTORY "${BITCOIN_SRC_ROOT}" COMMAND ./configure --disable-wallet --disable-tests --disable-bench)
  execute_process(WORKING_DIRECTORY "${BITCOIN_SRC_ROOT}" COMMAND make ${MAKE_JOBS})
endif()

foreach(BITCOIN_LIBRARIE ${BITCOIN_LIBRARIES})
  if (NOT EXISTS ${BITCOIN_LIBRARIE})
    message(FATAL_ERROR "${BITCOIN_LIBRARIE} not exists!")
  endif()
endforeach()


#
# libsecp256k1
#
set(secp256k1_DIR "${BITCOIN_SRC_ROOT}/src/secp256k1")
set(secp256k1_LIBRARIES "${secp256k1_DIR}/.libs/libsecp256k1.a")
if (NOT EXISTS ${secp256k1_LIBRARIES})
  message(STATUS "build secp256k1...")
  execute_process(WORKING_DIRECTORY "${secp256k1_DIR}" COMMAND ./autogen.sh)
  execute_process(WORKING_DIRECTORY "${secp256k1_DIR}" COMMAND ./configure --enable-module-recovery)
  execute_process(WORKING_DIRECTORY "${secp256k1_DIR}" COMMAND make ${MAKE_JOBS})
endif()
if (NOT EXISTS ${secp256k1_LIBRARIES})
  message(FATAL_ERROR "build secp256k1 failed!")
endif()

find_package(OpenSSL)
if(NOT OPENSSL_FOUND)
  message(FATAL_ERROR "OpenSSL not found!")
endif()

find_package(LibConfig)
if(NOT LIBCONFIG_FOUND)
  message(FATAL_ERROR "LibConfig not found!")
endif()

find_package(Glog)
if(NOT GLOG_FOUND)
message(FATAL_ERROR "Glog not found!")
endif()

find_package(libzmq)
if(NOT LIBZMQ_FOUND)
  message(FATAL_ERROR "libzmq not found!")
endif()

find_package(CURL)
if(NOT CURL_FOUND)
  message(FATAL_ERROR "libcurl not found!")
endif()

find_package(LibEvent)
if(NOT LibEvent_FOUND)
  message(FATAL_ERROR "libevent2 not found!")
endif()

find_package(Boost 1.36.0 COMPONENTS thread filesystem system regex program_options)
if(NOT Boost_FOUND)
  message(FATAL_ERROR "Boost not found!")
endif()

find_package(ZookeeperC)
if(NOT ZOOKEEPER_FOUND)
  message(FATAL_ERROR "libzookeeper-mt not found!")
endif()

find_package(LibGMP)
if(NOT GMP_FOUND)
  message(FATAL_ERROR "libgmp not found!")
endif()

find_package(KafkaC)
if(NOT KAFKA_FOUND)
  message(FATAL_ERROR "librdkafka not found!")
endif()
message("") # add an empty line

execute_process(COMMAND mysql_config --libs_r OUTPUT_VARIABLE MYSQL_LIB OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND mysql_config --include OUTPUT_VARIABLE MYSQL_INCLUDE OUTPUT_STRIP_TRAILING_WHITESPACE)
message("-- MySQL include: ${MYSQL_INCLUDE}")
message("-- MySQL library: ${MYSQL_LIB}")
if(NOT MYSQL_LIB)
  message(FATAL_ERROR "MySQL library not found!")
endif()
if(NOT MYSQL_INCLUDE)
  message(FATAL_ERROR "MySQL header not found!")
endif()
message("") # add an empty line

include_directories(src test ${BITCOIN_SRC_ROOT}/src ${BITCOIN_SRC_ROOT}/src/config ${BITCOIN_SRC_ROOT}/src/secp256k1/include
                    ${OPENSSL_INCLUDE_DIR} ${Boost_INCLUDE_DIRS} ${LIBZMQ_INCLUDE_DIR} ${GLOG_INCLUDE_DIRS}
                    ${LIBEVENT_INCLUDE_DIR} ${MYSQL_INCLUDE})
set(THIRD_LIBRARIES ${BITCOIN_LIBRARIES} ${OPENSSL_CRYPTO_LIBRARY} ${OPENSSL_SSL_LIBRARY} ${Boost_LIBRARIES} ${LIBCONFIGPP_LIBRARY}
                    ${LIBZMQ_LIBRARIES} ${GLOG_LIBRARIES} ${CURL_LIBRARIES} ${ZOOKEEPER_LIBRARIES} ${KAFKA_LIBRARIES} ${LIBEVENT_LIB}
                    ${LIBEVENT_PTHREADS_LIB} ${secp256k1_LIBRARIES} pthread ${MYSQL_LIB} ${GMP_LIBRARIES})

file(GLOB LIB_SOURCES src/*.cc)
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


###
# work with stratum switcher
###
option(POOL__WORK_WITH_STRATUM_SWITCHER
"Work with Stratum Switcher" OFF)

if(POOL__WORK_WITH_STRATUM_SWITCHER)
  message("-- Work with Stratum Switcher (-DPOOL__WORK_WITH_STRATUM_SWITCHER=ON)")
  add_definitions(-DWORK_WITH_STRATUM_SWITCHER)
  set(POOL__DEB_PACKNAME_POSTFIX "-withswitcher")
else()
  message("-- Work without Stratum Switcher (-DPOOL__WORK_WITH_STRATUM_SWITCHER=OFF)")
  set(POOL__DEB_PACKNAME_POSTFIX "")
endif()


###
# `make install` support
###

# install prefix
if(NOT POOL__INSTALL_PREFIX)
  set(POOL__INSTALL_PREFIX "/work/btcpool.btc")
endif()
message("-- Package Install Prefix (-DPOOL__INSTALL_PREFIX=<path>): ${POOL__INSTALL_PREFIX}")
set(CMAKE_INSTALL_PREFIX ${POOL__INSTALL_PREFIX})

set(PACKAGE_TMP_DIR ${CMAKE_BINARY_DIR}/package_tmp)

# init folders & copy configure files
install(CODE "execute_process(WORKING_DIRECTORY ${CMAKE_BINARY_DIR} COMMAND bash ${CMAKE_SOURCE_DIR}/install/init_package_folders.sh)")

# install targets , dirs & files
install(TARGETS
            blkmaker
            gbtmaker
            jobmaker
            nmcauxmaker
            poolwatcher
            sharelogger
            simulator
            slparser
            sserver
            statshttpd
            unittest
        DESTINATION
            ${POOL__INSTALL_PREFIX}/build)

install(FILES
            INSTALL.md
            LICENSE
            README.md
        DESTINATION
            ${POOL__INSTALL_PREFIX})

install(DIRECTORY
            docs
            install
            docker
        DESTINATION
            ${POOL__INSTALL_PREFIX})

install(DIRECTORY
            ${PACKAGE_TMP_DIR}/run_blkmaker
            ${PACKAGE_TMP_DIR}/run_gbtmaker
            ${PACKAGE_TMP_DIR}/run_jobmaker
            ${PACKAGE_TMP_DIR}/run_nmcauxmaker
            ${PACKAGE_TMP_DIR}/run_poolwatcher
            ${PACKAGE_TMP_DIR}/run_sharelogger
            ${PACKAGE_TMP_DIR}/run_simulator
            ${PACKAGE_TMP_DIR}/run_slparser
            ${PACKAGE_TMP_DIR}/run_sserver
            ${PACKAGE_TMP_DIR}/run_statshttpd
        DESTINATION
            ${POOL__INSTALL_PREFIX}/build)


###
# Debian/Ubuntu software package
###
if(POOL__GENERATE_DEB_PACKAGE)
  message("-- Generate Debian/Ubuntu software package (-DPOOL__GENERATE_DEB_PACKAGE=ON)")
  set(CPACK_GENERATOR "DEB")
  set(CPACK_PACKAGE_NAME "btcpool-bitcoin${POOL__DEB_PACKNAME_POSTFIX}")
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "YihaoPeng yihao.peng@bitmain.com")

  # the dependencies
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
  set(CPACK_DEBIAN_PACKAGE_DEPENDS "librdkafka1 (>= 0.9.1-1)")
else()
  message("-- Don't generate Debian/Ubuntu software package (-DPOOL__GENERATE_DEB_PACKAGE=OFF)")
endif()

include(CPack)
