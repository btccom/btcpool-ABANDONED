cmake_minimum_required (VERSION 2.6)

project (BTCPOOL)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")

###################################### Options ######################################
message("") # empty line
message("------------------------------- Options -------------------------------")

# packname postfix will be different with some optional feature enabled
set(POOL__DEB_PACKNAME_POSTFIX "")

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

# compiler options
set(CMAKE_CXX_COMPILER "g++")
set(CMAKE_C_COMPILER "gcc")
set(CMAKE_CXX_COMPILER_ARG1 "-std=c++0x")

# The number of concurrent jobs when compiling a third-party library
if(JOBS)
  message("-- Concurrent Jobs: ${JOBS} (-DJOBS=${JOBS})")
  set(MAKE_JOBS "-j${JOBS}") 
else()
  message("-- Concurrent Jobs: 1 (-DJOBS=1)")
  set(MAKE_JOBS "") 
endif()

#
# Chain Type
#
if(NOT CHAIN_TYPE)
  message(FATAL_ERROR "Chain type not defined! (-DCHAIN_TYPE=BTC|BCH|UBTC|SBTC)")
endif()
# Bitcoin
if (CHAIN_TYPE STREQUAL "BTC")
  set(CHAIN_NAME "Bitcoin Core")
  set(POOL__DEB_PACKNAME_POSTFIX "${POOL__DEB_PACKNAME_POSTFIX}-bitcoin")
  set(POOL__DEFAULT_INSTALL_PREFIX "/work/btcpool.btc")

  add_definitions(-DCHAIN_TYPE_BTC)
  add_definitions(-DKAFKA_TOPIC_PREFIX="") # for compatibility with old deployments, let it empty.
  add_definitions(-DZOOKEEPER_NODE_POSTFIX="") # for compatibility with old deployments, let it empty.

# BitcoinCash
elseif(CHAIN_TYPE STREQUAL "BCH")
  set(CHAIN_NAME "Bitcoin ABC")
  set(POOL__DEB_PACKNAME_POSTFIX "${POOL__DEB_PACKNAME_POSTFIX}-bitcoincash")
  set(POOL__DEFAULT_INSTALL_PREFIX "/work/btcpool.bcc") # for compatibility with old deployments, use bcc instead of bch.

  add_definitions(-DCHAIN_TYPE_BCH)
  add_definitions(-DKAFKA_TOPIC_PREFIX="") # for compatibility with old deployments, let it empty.
  add_definitions(-DZOOKEEPER_NODE_POSTFIX="") # for compatibility with old deployments, let it empty.

# UnitiedBitcoin
elseif(CHAIN_TYPE STREQUAL "UBTC")
  set(CHAIN_NAME "United Bitcoin")
  set(POOL__DEB_PACKNAME_POSTFIX "${POOL__DEB_PACKNAME_POSTFIX}-unitedbitcoin")
  set(POOL__DEFAULT_INSTALL_PREFIX "/work/btcpool.ubtc")

  add_definitions(-DCHAIN_TYPE_UBTC)
  add_definitions(-DKAFKA_TOPIC_PREFIX="UBTC_")
  add_definitions(-DZOOKEEPER_NODE_POSTFIX="_ubtc")

# SuperBitcoin
elseif(CHAIN_TYPE STREQUAL "SBTC")
  set(CHAIN_NAME "Super Bitcoin")
  set(POOL__DEB_PACKNAME_POSTFIX "${POOL__DEB_PACKNAME_POSTFIX}-superbitcoin")
  set(POOL__DEFAULT_INSTALL_PREFIX "/work/btcpool.sbtc")

  add_definitions(-DCHAIN_TYPE_SBTC)
  add_definitions(-DKAFKA_TOPIC_PREFIX="SBTC_")
  add_definitions(-DZOOKEEPER_NODE_POSTFIX="_sbtc")

# Unknown
else()
  message("Unknown chain type! (-DCHAIN_TYPE=${CHAIN_TYPE})")
  message(FATAL_ERROR "Supported chain type: BTC|BCH|UBTC|SBTC")
endif()

message("-- Chain Type: ${CHAIN_NAME} (-DCHAIN_TYPE=${CHAIN_TYPE})")


# add a macro that needed by chain's source code
add_definitions(-DHAVE_CONFIG_H)


#
# Root Dir of Chain's Source Code
#
if(NOT CHAIN_SRC_ROOT)
  message(FATAL_ERROR "Root dir of chain's source code not defined! (-DCHAIN_SRC_ROOT=<path>)")
endif()
message("-- Root Dir of ${CHAIN_NAME}: ${CHAIN_SRC_ROOT} (-DCHAIN_SRC_ROOT=${CHAIN_SRC_ROOT})")
if (NOT EXISTS "${CHAIN_SRC_ROOT}/src/version.h")
  message(FATAL_ERROR "The source code of ${CHAIN_NAME} not exists!")
endif()


###
# work with stratum switcher
###
option(POOL__WORK_WITH_STRATUM_SWITCHER "Work with Stratum Switcher" OFF)

if(POOL__WORK_WITH_STRATUM_SWITCHER)
  message("-- Work with Stratum Switcher: Enabled (-DPOOL__WORK_WITH_STRATUM_SWITCHER=ON)")
  add_definitions(-DWORK_WITH_STRATUM_SWITCHER)
  set(POOL__DEB_PACKNAME_POSTFIX "${POOL__DEB_PACKNAME_POSTFIX}-withswitcher")
else()
  message("-- Work with Stratum Switcher: Disabled (-DPOOL__WORK_WITH_STRATUM_SWITCHER=OFF)")
endif()


###
# user defined coinbase info
###
option(POOL__USER_DEFINED_COINBASE "User Defined Coinbase" OFF)

# the size of user defined coinbase, default is 10 bytes
if (NOT POOL__USER_DEFINED_COINBASE_SIZE)
  set(POOL__USER_DEFINED_COINBASE_SIZE 10)
endif()

if (POOL__USER_DEFINED_COINBASE)
  message("-- User Defined Coinbase: Enabled (-DPOOL__USER_DEFINED_COINBASE=ON)")
  message("-- The Size of User Defined Coinbase: ${POOL__USER_DEFINED_COINBASE_SIZE} Bytes (-DPOOL__USER_DEFINED_COINBASE_SIZE=${POOL__USER_DEFINED_COINBASE_SIZE})")
  add_definitions(-DUSER_DEFINED_COINBASE)
  add_definitions(-DUSER_DEFINED_COINBASE_SIZE=${POOL__USER_DEFINED_COINBASE_SIZE})
  set(POOL__DEB_PACKNAME_POSTFIX "${POOL__DEB_PACKNAME_POSTFIX}-usercoinbase${POOL__USER_DEFINED_COINBASE_SIZE}")
else()
  message("-- User Defined Coinbase: Disabled (-DPOOL__USER_DEFINED_COINBASE=OFF)")
endif()


###
# options for install & package
###

# install prefix
if(NOT POOL__INSTALL_PREFIX)
  set(POOL__INSTALL_PREFIX ${POOL__DEFAULT_INSTALL_PREFIX})
endif()
message("-- Install Prefix: ${POOL__INSTALL_PREFIX} (-DPOOL__INSTALL_PREFIX=${POOL__INSTALL_PREFIX})")
set(CMAKE_INSTALL_PREFIX ${POOL__INSTALL_PREFIX})

# Debian/Ubuntu software package
if(POOL__GENERATE_DEB_PACKAGE)
  message("-- Generate Debian/Ubuntu software package: Enabled (-DPOOL__GENERATE_DEB_PACKAGE=ON)")
else()
  message("-- Generate Debian/Ubuntu software package: Disabled (-DPOOL__GENERATE_DEB_PACKAGE=OFF)")
endif()


###################################### Libs ######################################
message("") # empty line
message("------------------------------- Libs -------------------------------")

#
# chain's libs
#
set(BITCOIN_LIBRARIES
  "${CHAIN_SRC_ROOT}/src/libbitcoin_common.a"
  "${CHAIN_SRC_ROOT}/src/libbitcoin_consensus.a"
  "${CHAIN_SRC_ROOT}/src/libbitcoin_util.a"
  "${CHAIN_SRC_ROOT}/src/crypto/libbitcoin_crypto.a")

list(GET BITCOIN_LIBRARIES 0 BITCOIN_LIBRARIE)
if (NOT EXISTS ${BITCOIN_LIBRARIE})
  message(STATUS "building ${CHAIN_NAME}...")
  execute_process(WORKING_DIRECTORY "${CHAIN_SRC_ROOT}" COMMAND ./autogen.sh)
  execute_process(WORKING_DIRECTORY "${CHAIN_SRC_ROOT}" COMMAND ./configure --with-gui=no --disable-wallet --disable-tests --disable-bench)
  execute_process(WORKING_DIRECTORY "${CHAIN_SRC_ROOT}" COMMAND make ${MAKE_JOBS})
endif()

foreach(BITCOIN_LIBRARIE ${BITCOIN_LIBRARIES})
  if (NOT EXISTS ${BITCOIN_LIBRARIE})
    message(FATAL_ERROR "${BITCOIN_LIBRARIE} not exists!")
  endif()
endforeach()


#
# libsecp256k1
#
set(secp256k1_DIR "${CHAIN_SRC_ROOT}/src/secp256k1")
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

find_package(LibHiredis)
if(NOT HIREDIS_FOUND)
  message(FATAL_ERROR "libhiredis not found!")
endif()

find_package(LibPthread)
if(NOT PTHREAD_FOUND)
  message(FATAL_ERROR "libpthread not found!")
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

###################################### Building Tools ######################################

# Find binutils
message("-- CMAKE_OBJCOPY: ${CMAKE_OBJCOPY}")
if(NOT CMAKE_OBJCOPY)
  message("-- CMAKE_OBJCOPY not found, debug info will not separated from targets")
  mark_as_advanced(CMAKE_OBJCOPY)
endif()

###################################### Targets ######################################

include_directories(src test ${CHAIN_SRC_ROOT}/src ${CHAIN_SRC_ROOT}/src/config ${CHAIN_SRC_ROOT}/src/secp256k1/include
                    ${OPENSSL_INCLUDE_DIR} ${Boost_INCLUDE_DIRS} ${LIBZMQ_INCLUDE_DIR} ${GLOG_INCLUDE_DIRS}
                    ${LIBEVENT_INCLUDE_DIR} ${MYSQL_INCLUDE})
set(THIRD_LIBRARIES ${BITCOIN_LIBRARIES} ${OPENSSL_CRYPTO_LIBRARY} ${OPENSSL_SSL_LIBRARY} ${Boost_LIBRARIES} ${LIBCONFIGPP_LIBRARY}
                    ${LIBZMQ_LIBRARIES} ${GLOG_LIBRARIES} ${CURL_LIBRARIES} ${ZOOKEEPER_LIBRARIES} ${KAFKA_LIBRARIES} ${LIBEVENT_LIB}
                    ${LIBEVENT_PTHREADS_LIB} ${secp256k1_LIBRARIES} ${PTHREAD_LIBRARIES} ${MYSQL_LIB} ${GMP_LIBRARIES} ${Hiredis_LIBRARIES} /usr/local/lib/libethash.a)

file(GLOB LIB_SOURCES src/*.cc src/rsk/*.cc)
add_library(btcpool STATIC ${LIB_SOURCES})

file(GLOB_RECURSE TEST_SOURCES test/*.cc)
add_executable(unittest ${TEST_SOURCES})
target_link_libraries(unittest btcpool ${THIRD_LIBRARIES})

file(GLOB_RECURSE GBTMAKER_SOURCES src/gbtmaker/*.cc)
add_executable(gbtmaker ${GBTMAKER_SOURCES})
target_link_libraries(gbtmaker btcpool ${THIRD_LIBRARIES})

set(GWMAKER_SOURCES src/gwmaker/GwMakerMain.cc)
add_executable(gwmaker ${GWMAKER_SOURCES})
target_link_libraries(gwmaker btcpool ${THIRD_LIBRARIES})

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


###################################### Install & Package ######################################
message("") # empty line
message("------------------------------- Install & Package -------------------------------")

###
# `make install` support
###

# targets
set(INSTALL_TARGETS
        blkmaker
        gbtmaker
        gwmaker
        jobmaker
        nmcauxmaker
        poolwatcher
        sharelogger
        simulator
        slparser
        sserver
        statshttpd)

# testcase
set(INSTALL_TEST_TARGETS
        unittest)

# tmp dir for install & packet
set(PACKAGE_TMP_DIR ${CMAKE_BINARY_DIR}/package_tmp)

# init folders & copy configure files
install(CODE "execute_process(WORKING_DIRECTORY ${CMAKE_BINARY_DIR} COMMAND bash ${CMAKE_SOURCE_DIR}/install/init_package_folders.sh)"
        COMPONENT main)

# separate debug info from targets
if(CMAKE_OBJCOPY)
  foreach(tgt ${INSTALL_TARGETS} ${INSTALL_TEST_TARGETS})
    add_custom_command(TARGET ${tgt} POST_BUILD
      COMMAND ${CMAKE_OBJCOPY} --only-keep-debug $<TARGET_FILE:${tgt}> $<TARGET_FILE:${tgt}>.dbg
      COMMAND ${CMAKE_OBJCOPY} --strip-debug $<TARGET_FILE:${tgt}>
      COMMAND ${CMAKE_OBJCOPY} --add-gnu-debuglink=$<TARGET_FILE:${tgt}>.dbg $<TARGET_FILE:${tgt}>)
  endforeach()
endif()

# install targets , dirs & files
foreach(tgt ${INSTALL_TARGETS})
  # binary
  install(PROGRAMS     ${CMAKE_BINARY_DIR}/${tgt}
          COMPONENT   main
          DESTINATION ${POOL__INSTALL_PREFIX}/build)
  # running dir
  install(DIRECTORY   ${PACKAGE_TMP_DIR}/run_${tgt}
          COMPONENT   main
          DESTINATION ${POOL__INSTALL_PREFIX}/build)
endforeach()

# test targets
foreach(tgt ${INSTALL_TEST_TARGETS})
  # binary
  install(PROGRAMS     ${CMAKE_BINARY_DIR}/${tgt}
          COMPONENT   main
          DESTINATION ${POOL__INSTALL_PREFIX}/build)
endforeach()

# documents
install(FILES
            LICENSE
            README.md
        COMPONENT
            main
        DESTINATION
            ${POOL__INSTALL_PREFIX})

# document dirss
install(DIRECTORY
            docs
            install
            docker
        COMPONENT
            main
        DESTINATION
            ${POOL__INSTALL_PREFIX})

# install debug info
if(CMAKE_OBJCOPY)
  foreach(tgt ${INSTALL_TARGETS} ${INSTALL_TEST_TARGETS})
    install(FILES       ${CMAKE_BINARY_DIR}/${tgt}.dbg
            COMPONENT   dbginfo
            DESTINATION ${POOL__INSTALL_PREFIX}/build)
  endforeach()
endif()

# generate deb package by CPack
if(POOL__GENERATE_DEB_PACKAGE)
  set(CPACK_GENERATOR "DEB")
  set(CPACK_PACKAGE_NAME "btcpool${POOL__DEB_PACKNAME_POSTFIX}")
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "YihaoPeng yihao.peng@bitmain.com")

  message("-- Package Name: ${CPACK_PACKAGE_NAME}")

  # version
  SET(CPACK_PACKAGE_VERSION_MAJOR "2")
  SET(CPACK_PACKAGE_VERSION_MINOR "0")
  SET(CPACK_PACKAGE_VERSION_PATCH "0")

  # components
  set(CPACK_DEB_COMPONENT_INSTALL ON)
  set(CPACK_DEB_USE_DISPLAY_NAME_IN_FILENAME ON)
  set(CPACK_DEBIAN_MAIN_PACKAGE_NAME ${CPACK_PACKAGE_NAME})
  set(CPACK_DEBIAN_DBGINFO_PACKAGE_NAME ${CPACK_PACKAGE_NAME}-dbg)

  # dependencies
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
  set(CPACK_DEBIAN_PACKAGE_DEPENDS "librdkafka1 (>= 0.9.1-1)")

  include(CPack)
endif()
