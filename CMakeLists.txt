cmake_minimum_required (VERSION 2.6)

project (BTCPOOL)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules")


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

if(CMAKE_BUILD_TYPE MATCHES Debug)
  set(CMAKE_CXX_FLAGS "-g -O0 -Wall")
elseif(CMAKE_BUILD_TYPE MATCHES Release)
  set(CMAKE_CXX_FLAGS "-g -O2 -Wall")
else()
  set(CMAKE_CXX_FLAGS "-g -Wall")
endif()

# compiler options
set(CMAKE_CXX_COMPILER "g++")
set(CMAKE_C_COMPILER "gcc")
# enable c++11
set(CMAKE_CXX_STANDARD 11)
# stop building after the first error
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fmax-errors=1")

# The number of concurrent jobs when compiling third-party libraries
if(JOBS)
  message("-- Concurrent Jobs: ${JOBS} (-DJOBS=${JOBS})")
  set(MAKE_JOBS "-j${JOBS}") 
else()
  message("-- Concurrent Jobs: 1 (-DJOBS=1)")
  set(MAKE_JOBS "") 
endif()


#
# params for all supported chains
#

set(SUPPORTED_CHAIN_TYPES "BTC,BCH,UBTC,SBTC")

# Bitcoin
set(CHAIN_NAME_BTC "Bitcoin Core")
set(POOL__DEB_PACKNAME_POSTFIX_BTC "-bitcoin")
set(POOL__DEFAULT_INSTALL_PREFIX_BTC "/work/btcpool.btc")
set(COMPILE_DEFINITIONS_BTC
        CHAIN_TYPE_BTC
        CHAIN_TYPE_STRING="BTC"
        KAFKA_TOPIC_PREFIX=""      # for compatibility with old deployments, let it empty.
        ZOOKEEPER_NODE_POSTFIX=""  # for compatibility with old deployments, let it empty.
        HAVE_CONFIG_H
   )

# BitcoinCash
set(CHAIN_NAME_BCH "Bitcoin ABC")
set(POOL__DEB_PACKNAME_POSTFIX_BCH "-bitcoincash")
set(POOL__DEFAULT_INSTALL_PREFIX_BCH "/work/btcpool.bcc") # for compatibility with old deployments, use bcc instead of bch.
set(COMPILE_DEFINITIONS_BCH
        CHAIN_TYPE_BCH
        CHAIN_TYPE_STRING="BCH"
        KAFKA_TOPIC_PREFIX=""      # for compatibility with old deployments, let it empty.
        ZOOKEEPER_NODE_POSTFIX=""  # for compatibility with old deployments, let it empty.
        HAVE_CONFIG_H
   )

# UnitiedBitcoin
set(CHAIN_NAME_UBTC "United Bitcoin")
set(POOL__DEB_PACKNAME_POSTFIX_UBTC "-unitedbitcoin")
set(POOL__DEFAULT_INSTALL_PREFIX_UBTC "/work/btcpool.ubtc")
set(COMPILE_DEFINITIONS_UBTC
        CHAIN_TYPE_UBTC
        CHAIN_TYPE_STRING="UBTC"
        KAFKA_TOPIC_PREFIX="UBTC_"
        ZOOKEEPER_NODE_POSTFIX="_ubtc"
        HAVE_CONFIG_H
   )

# SuperBitcoin
set(CHAIN_NAME_SBTC "Super Bitcoin")
set(POOL__DEB_PACKNAME_POSTFIX_SBTC "-superbitcoin")
set(POOL__DEFAULT_INSTALL_PREFIX_SBTC "/work/btcpool.sbtc")
set(COMPILE_DEFINITIONS_SBTC
        CHAIN_TYPE_UBTC
        CHAIN_TYPE_STRING="SBTC"
        KAFKA_TOPIC_PREFIX="SBTC_"
        ZOOKEEPER_NODE_POSTFIX="_sbtc"
        HAVE_CONFIG_H
   )

# macros for add a definition to a chain
macro(AddChainDefinition chainType definition)
  set(COMPILE_DEFINITIONS_${chainType} ${COMPILE_DEFINITIONS_${chainType}} ${definition})
endmacro()
# macros for add a definition to all chains' libraries
macro(AddAllChainDefinition definition)
  foreach(chainType ${CHAIN_TYPES})
    set(COMPILE_DEFINITIONS_${chainType} ${COMPILE_DEFINITIONS_${chainType}} ${definition})
  endforeach()

  # add the definition to executables
  add_definitions(-D${definition})
endmacro()

#
# Chain Types
#
if(NOT CHAIN_TYPES)
  message(FATAL_ERROR "Chain types not defined! (example: -DCHAIN_TYPES=${SUPPORTED_CHAIN_TYPES})")
endif()
message("-- Chain Types: ${CHAIN_TYPES} (-DCHAIN_TYPES=${CHAIN_TYPES})")

# convert CHAIN_TYPES from string to list
string(REPLACE "," ";" CHAIN_TYPES ${CHAIN_TYPES})

# check if the chain is supported and the source code exists
foreach(chainType ${CHAIN_TYPES})
  if(CHAIN_NAME_${chainType})
    message("    ${CHAIN_NAME_${chainType}} (${chainType}) enabled.")
  else()
    message(FATAL_ERROR "Unknown chain type: ${chainType}\nSupported chain types: ${SUPPORTED_CHAIN_TYPES}")
  endif()

  if (NOT EXISTS "${CHAIN_${chainType}_SRC}/src/version.h")
    message(FATAL_ERROR "Invalid CHAIN_${chainType}_SRC. ${CHAIN_${chainType}_SRC}/src/version.h not exists!")
  endif()
endforeach()

# set install prefix and package name postfix
list(LENGTH CHAIN_TYPES chainNum)
if(chainNum EQUAL 1)
  # only one chain, static linking
  set(POOL__DEB_PACKNAME_POSTFIX "${POOL__DEB_PACKNAME_POSTFIX}${POOL__DEB_PACKNAME_POSTFIX_${CHAIN_TYPES}}")
  set(POOL__DEFAULT_INSTALL_PREFIX "${POOL__DEFAULT_INSTALL_PREFIX_${CHAIN_TYPES}}")
  set(DYNAMIC_LOAD_BITCOIN OFF)
  list(GET CHAIN_TYPES 0 CHAIN_TYPE)
else()
  # more than one chains, dynamic linking & dynamic loading
  set(POOL__DEB_PACKNAME_POSTFIX "${POOL__DEB_PACKNAME_POSTFIX}-multichains")
  set(POOL__DEFAULT_INSTALL_PREFIX "/work/btcpool.multi")
  set(DYNAMIC_LOAD_BITCOIN ON)
endif()

#
# Root Dir of Chain's Source Code
#
message("-- Root directory of chain's source code:")
foreach(chainType ${CHAIN_TYPES})
  if(CHAIN_${chainType}_SRC)
    message("    ${CHAIN_NAME_${chainType}} (${chainType}): ${CHAIN_${chainType}_SRC} (-DCHAIN_${chainType}_SRC=${CHAIN_${chainType}_SRC})")
  else()
    message(FATAL_ERROR "Root directory of ${chainType}'s source code not defined!\n(example: -DCHAIN_${chainType}_SRC=<path>)")
  endif()
endforeach()

#
# reconfigure & rebuild each chain's source code
#
if(CHAIN_REBUILD)
    message("-- Clean each chain's old build: Enabled (-DCHAIN_REBUILD=ON)")
  else()
    message("-- Clean each chain's old build: Disabled (-DCHAIN_REBUILD=OFF)")
endif()

###
# work with stratum switcher
###
option(POOL__WORK_WITH_STRATUM_SWITCHER "Work with Stratum Switcher" OFF)

if(POOL__WORK_WITH_STRATUM_SWITCHER)
  message("-- Work with Stratum Switcher: Enabled (-DPOOL__WORK_WITH_STRATUM_SWITCHER=ON)")
  AddAllChainDefinition(WORK_WITH_STRATUM_SWITCHER)
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
  AddAllChainDefinition(USER_DEFINED_COINBASE)
  AddAllChainDefinition(USER_DEFINED_COINBASE_SIZE=${POOL__USER_DEFINED_COINBASE_SIZE})
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


###################################### Libraries ######################################
message("") # empty line
message("------------------------------- Libraries -------------------------------")

#
# chain's libs
#
set(BITCOIN_LIBRARIES
  "src/libbitcoin_common.a"
  "src/libbitcoin_consensus.a"
  "src/libbitcoin_util.a"
  "src/crypto/libbitcoin_crypto.a")

list(GET BITCOIN_LIBRARIES 0 BITCOIN_LIBRARIE_COMMON)

foreach(chainType ${CHAIN_TYPES})
  # full paths of the chain's libraries
  set(BITCOIN_LIBRARIES_${chainType} "")

  # check if the source code need building
  if (CHAIN_REBUILD OR NOT EXISTS ${CHAIN_${chainType}_SRC}/${BITCOIN_LIBRARIE_COMMON})
    message(STATUS "building ${CHAIN_NAME_${chainType}} (${chainType})...")

    message("running \"${CHAIN_${chainType}_SRC}/autogen.sh\"...")
    execute_process(WORKING_DIRECTORY "${CHAIN_${chainType}_SRC}" COMMAND ./autogen.sh)
    
    message("running \"${CHAIN_${chainType}_SRC}/configure\"...")
    execute_process(WORKING_DIRECTORY "${CHAIN_${chainType}_SRC}" COMMAND ./configure
                                                                              --with-gui=no --disable-wallet
                                                                              --disable-tests --disable-bench
                                                                              CFLAGS=-fPIC CXXFLAGS=-fPIC)
    
    if (CHAIN_REBUILD)
      message("running \"make clean\"...")
      execute_process(WORKING_DIRECTORY "${CHAIN_${chainType}_SRC}" COMMAND make clean)
    endif()

    message("running \"make ${MAKE_JOBS}\"...")
    execute_process(WORKING_DIRECTORY "${CHAIN_${chainType}_SRC}" COMMAND make ${MAKE_JOBS})
  endif()

  # check and set all libraries' path
  message("-- Libraries of ${CHAIN_NAME_${chainType}} (${chainType}):")
  set(BITCOIN_INCLUDE_DIR_${chainType} ${CHAIN_${chainType}_SRC}/src ${CHAIN_${chainType}_SRC}/src/config)
  foreach(BITCOIN_LIBRARIE ${BITCOIN_LIBRARIES})
    if (EXISTS ${CHAIN_${chainType}_SRC}/${BITCOIN_LIBRARIE})
      set(BITCOIN_LIBRARIES_${chainType} ${BITCOIN_LIBRARIES_${chainType}} ${CHAIN_${chainType}_SRC}/${BITCOIN_LIBRARIE})
      message("    ${CHAIN_${chainType}_SRC}/${BITCOIN_LIBRARIE}")
    else()
      set(msg "build ${CHAIN_NAME_${chainType}} (${chainType}) failed:")
      set(msg "${msg}\n${CHAIN_${chainType}_SRC}/${BITCOIN_LIBRARIE} not exists!")
      message(FATAL_ERROR ${msg})
    endif()
  endforeach()
endforeach()

#
# libsecp256k1
#
foreach(chainType ${CHAIN_TYPES})
  set(secp256k1_DIR_${chainType} "${CHAIN_${chainType}_SRC}/src/secp256k1")
  set(secp256k1_INCLUDE_DIR_${chainType} "${secp256k1_DIR_${chainType}}/include")
  set(secp256k1_LIBRARIES_${chainType} "${secp256k1_DIR_${chainType}}/.libs/libsecp256k1.a")

  if (NOT EXISTS ${secp256k1_LIBRARIES_${chainType}})
    message(STATUS "build secp256k1 of ${CHAIN_NAME_${chainType}} (${chainType})...")

    message("running \"${secp256k1_DIR_${chainType}}/autogen.sh\"...")
    execute_process(WORKING_DIRECTORY "${secp256k1_DIR_${chainType}}" COMMAND ./autogen.sh)

    message("running \"${secp256k1_DIR_${chainType}}/configure\"...")
    execute_process(WORKING_DIRECTORY "${secp256k1_DIR_${chainType}}" COMMAND ./configure
                                                                                --enable-module-recovery
                                                                                CFLAGS=-fPIC CXXFLAGS=-fPIC)
    
    message("running \"make ${MAKE_JOBS}\"...")
    execute_process(WORKING_DIRECTORY "${secp256k1_DIR_${chainType}}" COMMAND make ${MAKE_JOBS})
  endif()

  if (NOT EXISTS ${secp256k1_LIBRARIES_${chainType}})
    set(msg "build secp256k1 of ${CHAIN_NAME_${chainType}} (${chainType}) failed:")
    set(msg "${msg}\n${secp256k1_LIBRARIES_${chainType}} not exists!")
    message(FATAL_ERROR ${msg})
  endif()

  message("-- libsecp256k1 of ${CHAIN_NAME_${chainType}} (${chainType}): ${secp256k1_LIBRARIES_${chainType}}")
endforeach()


#
# other libraries
#

find_package(OpenSSL)
if(NOT OPENSSL_FOUND)
  message(FATAL_ERROR "OpenSSL not found!")
endif()

find_package(LibConfig)
if(NOT LIBCONFIGPP_FOUND)
  message(FATAL_ERROR "LibConfig++ not found!")
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

find_package(LibMysqlClient)
if(NOT MYSQL_FOUND)
  message(FATAL_ERROR "MySQL library not found!")
endif()

find_package(LibDL)
if(NOT LIBDL_FOUND)
  message(FATAL_ERROR "Dynamic library support (libdl) not found!")
endif()

find_package(KafkaC)
if(NOT KAFKA_FOUND)
  message(FATAL_ERROR "librdkafka not found!")
endif()


###################################### Building Tools ######################################
message("") # empty line
message("------------------------------- Building Tools -------------------------------")

# Find command `objcopy`
if(CMAKE_OBJCOPY)
  message("-- CMAKE_OBJCOPY: ${CMAKE_OBJCOPY}")
else()
  message("-- CMAKE_OBJCOPY not found, debug info will not separated from targets")
endif()


###################################### Targets ######################################
message("") # empty line
message("------------------------------- Targets -------------------------------")

set(COMMON_INCLUDE_DIRS ${OPENSSL_INCLUDE_DIR} ${LIBCONFIGPP_INCLUDE_DIR} ${GLOG_INCLUDE_DIRS} ${LIBZMQ_INCLUDE_DIR}
                        ${CURL_INCLUDE_DIR} ${LIBEVENT_INCLUDE_DIR} ${Boost_INCLUDE_DIRS} ${ZOOKEEPER_INCLUDE_DIRS}
                        ${GMP_INCLUDE_DIR} ${Hiredis_INCLUDE_DIRS} ${PTHREAD_INCLUDE_DIR} ${MYSQL_INCLUDE_DIR}
                        ${LIBDL_INCLUDE_DIR} ${KAFKA_INCLUDE_DIR}
                        ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/test)

set(COMMON_LIBRARIES ${OPENSSL_CRYPTO_LIBRARY} ${OPENSSL_SSL_LIBRARY} ${LIBCONFIGPP_LIBRARY} ${GLOG_LIBRARIES} ${LIBZMQ_LIBRARIES}
                     ${CURL_LIBRARIES} ${LIBEVENT_LIB} ${LIBEVENT_PTHREADS_LIB} ${Boost_LIBRARIES} ${ZOOKEEPER_LIBRARIES}
                     ${GMP_LIBRARIES} ${Hiredis_LIBRARIES} ${PTHREAD_LIBRARIES} ${MYSQL_LIBRARIES} ${KAFKA_LIBRARIES}
                     ${LIBDL_LIBRARIES})

# dynamic loader
set(DYWRAPPER_SOURCES src/dynamicloader/DynamicWrapper.cc)
set(DYLOADER_SOURCES src/dynamicloader/DynamicLoader.cc)

# library name of btcpool
set(LIBBTCPOOL_BITCOIN_BASENAME "btcpool-bitcoin-")
if(DYNAMIC_LOAD_BITCOIN)
  set(LIBBTCPOOL_BITCOIN_PREFIX "${CMAKE_SHARED_LIBRARY_PREFIX}${LIBBTCPOOL_BITCOIN_BASENAME}")
  set(LIBBTCPOOL_BITCOIN_POSTFIX "${CMAKE_SHARED_LIBRARY_SUFFIX}")
else()
  set(LIBBTCPOOL_BITCOIN_PREFIX "${CMAKE_STATIC_LIBRARY_PREFIX}${LIBBTCPOOL_BITCOIN_BASENAME}")
  set(LIBBTCPOOL_BITCOIN_POSTFIX "${CMAKE_STATIC_LIBRARY_SUFFIX}")
endif()

# add definitions for all chains
if(DYNAMIC_LOAD_BITCOIN)
  AddAllChainDefinition(DYNAMIC_LOAD_LIBBTCPOOL_BITCOIN)
  AddAllChainDefinition(LIBBTCPOOL_BITCOIN_PREFIX="${LIBBTCPOOL_BITCOIN_PREFIX}")
  AddAllChainDefinition(LIBBTCPOOL_BITCOIN_POSTFIX="${LIBBTCPOOL_BITCOIN_POSTFIX}")
else()
  foreach(definition ${COMPILE_DEFINITIONS_${CHAIN_TYPE}})
    # set for executables
    add_definitions(-D${definition})
  endforeach()
endif()

# build btcpool as shared or static library
file(GLOB LIBBTCPOOL_SOURCES src/*.cc src/rsk/*.cc ${DYWRAPPER_SOURCES})
foreach(chainType ${CHAIN_TYPES})
  set(BTCPOOL_LIBRARY_${chainType} ${LIBBTCPOOL_BITCOIN_BASENAME}${chainType})
  set(BTCPOOL_LIBRARY_${chainType}_FULLNAME ${LIBBTCPOOL_BITCOIN_PREFIX}${chainType}${LIBBTCPOOL_BITCOIN_POSTFIX})

  if(DYNAMIC_LOAD_BITCOIN)
    add_library(${BTCPOOL_LIBRARY_${chainType}} SHARED ${LIBBTCPOOL_SOURCES})
    target_link_libraries(${BTCPOOL_LIBRARY_${chainType}}
                          ${COMMON_LIBRARIES} ${BITCOIN_LIBRARIES_${chainType}} ${secp256k1_LIBRARIES_${chainType}})
    message("-- Dynamic library: ${BTCPOOL_LIBRARY_${chainType}_FULLNAME}")
  else()
    add_library(${BTCPOOL_LIBRARY_${chainType}} STATIC ${LIBBTCPOOL_SOURCES})
    message("-- Static library: ${BTCPOOL_LIBRARY_${chainType}_FULLNAME}")
  endif()

  # set definitions & include directories
  set_property(TARGET   ${BTCPOOL_LIBRARY_${chainType}}
               PROPERTY COMPILE_DEFINITIONS
                        ${COMPILE_DEFINITIONS_${chainType}})
  set_property(TARGET   ${BTCPOOL_LIBRARY_${chainType}}
               PROPERTY INCLUDE_DIRECTORIES
                        ${COMMON_INCLUDE_DIRS}
                        ${BITCOIN_INCLUDE_DIR_${chainType}}
                        ${secp256k1_INCLUDE_DIR_${chainType}})
endforeach()

# libraries linking with below targets
if(DYNAMIC_LOAD_BITCOIN)
  set(TARGET_LIBRARIES ${COMMON_LIBRARIES})
else()
  # CHAIN_TYPE will be defined at below if only one chain enabled
  set(TARGET_LIBRARIES  ${BTCPOOL_LIBRARY_${CHAIN_TYPE}}
                        ${BITCOIN_LIBRARIES_${CHAIN_TYPE}}
                        ${secp256k1_LIBRARIES_${CHAIN_TYPE}}
                        ${COMMON_LIBRARIES})
endif()

#
# executables
#

include_directories(src test)

# unittests
file(GLOB_RECURSE TEST_SOURCES test/*.cc)
foreach(chainType ${CHAIN_TYPES})
  add_executable(unittest-${chainType} ${TEST_SOURCES})
  # set definitions & include directories
  set_property(TARGET   unittest-${chainType}
               PROPERTY COMPILE_DEFINITIONS
                        ${COMPILE_DEFINITIONS_${chainType}})
  set_property(TARGET   unittest-${chainType}
               PROPERTY INCLUDE_DIRECTORIES
                        ${COMMON_INCLUDE_DIRS}
                        ${BITCOIN_INCLUDE_DIR_${chainType}}
                        ${secp256k1_INCLUDE_DIR_${chainType}})
  # link to libraries
  target_link_libraries(unittest-${chainType}
                            ${BTCPOOL_LIBRARY_${chainType}}
                            ${BITCOIN_LIBRARIES_${chainType}}
                            ${secp256k1_LIBRARIES_${chainType}}
                            ${COMMON_LIBRARIES})
  message("-- Executable: unittest-${chainType}")
endforeach()

# gbtmaker
file(GLOB_RECURSE GBTMAKER_SOURCES src/gbtmaker/*.cc ${DYLOADER_SOURCES})
add_executable(gbtmaker ${GBTMAKER_SOURCES})
target_link_libraries(gbtmaker ${TARGET_LIBRARIES})
message("-- Executable: gbtmaker")

# gwmaker
set(GWMAKER_SOURCES src/gwmaker/GwMakerMain.cc ${DYLOADER_SOURCES})
add_executable(gwmaker ${GWMAKER_SOURCES})
target_link_libraries(gwmaker ${TARGET_LIBRARIES})
message("-- Executable: gwmaker")

# nmcauxmaker
file(GLOB_RECURSE NMC_AUXBLOCK_MAKER_SOURCES src/nmcauxmaker/*.cc ${DYLOADER_SOURCES})
add_executable(nmcauxmaker ${NMC_AUXBLOCK_MAKER_SOURCES})
target_link_libraries(nmcauxmaker ${TARGET_LIBRARIES})
message("-- Executable: nmcauxmaker")

# jobmaker
file(GLOB_RECURSE JOBMAKER_SOURCES src/jobmaker/*.cc ${DYLOADER_SOURCES})
add_executable(jobmaker ${JOBMAKER_SOURCES})
target_link_libraries(jobmaker ${TARGET_LIBRARIES})
message("-- Executable: jobmaker")

# sserver
file(GLOB_RECURSE SSERVER_SOURCES src/sserver/*.cc ${DYLOADER_SOURCES})
add_executable(sserver ${SSERVER_SOURCES})
target_link_libraries(sserver ${TARGET_LIBRARIES})
message("-- Executable: sserver")

# statshttpd
file(GLOB_RECURSE STATSHTTPD_SOURCES src/statshttpd/*.cc ${DYLOADER_SOURCES})
add_executable(statshttpd ${STATSHTTPD_SOURCES})
target_link_libraries(statshttpd ${TARGET_LIBRARIES})
message("-- Executable: statshttpd")

# sharelogger
file(GLOB_RECURSE SHARELOGGER_SOURCES src/sharelogger/*.cc ${DYLOADER_SOURCES})
add_executable(sharelogger ${SHARELOGGER_SOURCES})
target_link_libraries(sharelogger ${TARGET_LIBRARIES})
message("-- Executable: sharelogger")

# slparser
file(GLOB_RECURSE SLPARSER_SOURCES src/slparser/*.cc src/Utils.cc ${DYLOADER_SOURCES})
add_executable(slparser ${SLPARSER_SOURCES})
target_link_libraries(slparser ${TARGET_LIBRARIES})
message("-- Executable: slparser")

# blkmaker
file(GLOB_RECURSE BLKMAKER_SOURCES src/blkmaker/*.cc  ${DYLOADER_SOURCES})
add_executable(blkmaker ${BLKMAKER_SOURCES})
target_link_libraries(blkmaker ${TARGET_LIBRARIES})
message("-- Executable: blkmaker")

# simulator
file(GLOB_RECURSE SIMULATOR_SOURCES src/simulator/*.cc ${DYLOADER_SOURCES})
add_executable(simulator ${SIMULATOR_SOURCES})
target_link_libraries(simulator ${TARGET_LIBRARIES})
message("-- Executable: simulator")

# poolwatcher
file(GLOB_RECURSE POOLWATCHER_SOURCES src/poolwatcher/*.cc ${DYLOADER_SOURCES})
add_executable(poolwatcher ${POOLWATCHER_SOURCES})
target_link_libraries(poolwatcher ${TARGET_LIBRARIES})
message("-- Executable: poolwatcher")


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

# unittests
set(INSTALL_TEST_TARGETS "")
foreach(chainType ${CHAIN_TYPES})
  set(INSTALL_TEST_TARGETS ${INSTALL_TEST_TARGETS} unittest-${chainType})
endforeach()

# shared libraries
set(INSTALL_DYLIB_TARGETS "")
if(DYNAMIC_LOAD_BITCOIN)
  foreach(chainType ${CHAIN_TYPES})
    set(INSTALL_DYLIB_TARGETS ${INSTALL_DYLIB_TARGETS} ${BTCPOOL_LIBRARY_${chainType}})
  endforeach()
endif()

# tmp dir for install & packet
set(PACKAGE_TMP_DIR ${CMAKE_BINARY_DIR}/package_tmp)

# init folders & copy configure files
install(CODE "execute_process(WORKING_DIRECTORY ${CMAKE_BINARY_DIR} COMMAND bash ${CMAKE_SOURCE_DIR}/install/init_package_folders.sh)"
        COMPONENT main)

# separate debug info from targets
if(CMAKE_OBJCOPY)
  foreach(tgt ${INSTALL_TARGETS} ${INSTALL_TEST_TARGETS} ${INSTALL_DYLIB_TARGETS})
    add_custom_command(TARGET ${tgt} POST_BUILD
      COMMAND ${CMAKE_OBJCOPY} --only-keep-debug $<TARGET_FILE:${tgt}> $<TARGET_FILE:${tgt}>.dbg
      COMMAND ${CMAKE_OBJCOPY} --strip-debug $<TARGET_FILE:${tgt}>
      COMMAND ${CMAKE_OBJCOPY} --add-gnu-debuglink=$<TARGET_FILE:${tgt}>.dbg $<TARGET_FILE:${tgt}>)
  endforeach()
endif()

# install targets , dirs & files
foreach(tgt ${INSTALL_TARGETS})
  # binary
  install(PROGRAMS    ${CMAKE_BINARY_DIR}/${tgt}
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
  install(PROGRAMS    ${CMAKE_BINARY_DIR}/${tgt}
          COMPONENT   main
          DESTINATION ${POOL__INSTALL_PREFIX}/build)
endforeach()

# shared libraries targets
foreach(tgt ${INSTALL_DYLIB_TARGETS})
  # binary
  install(FILES       ${CMAKE_BINARY_DIR}/${CMAKE_SHARED_LIBRARY_PREFIX}${tgt}${CMAKE_SHARED_LIBRARY_SUFFIX}
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

  foreach(tgt ${INSTALL_DYLIB_TARGETS})
    install(FILES       ${CMAKE_BINARY_DIR}/${CMAKE_SHARED_LIBRARY_PREFIX}${tgt}${CMAKE_SHARED_LIBRARY_SUFFIX}.dbg
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
  SET(CPACK_PACKAGE_VERSION_MINOR "1")
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


###################################### Tips ######################################
message("") # empty line
message("------------------------------- Tips -------------------------------\n")

message("Running `make` or `make -jn` (example: `make -j4`) to build\n")

if(DYNAMIC_LOAD_BITCOIN)
  message("If make failed with message \"recompile with -fPIC\", run cmake with \"-DCHAIN_REBUILD=ON\"\n")
endif()


###################################### Endding ######################################
message("--------------------------------------------------------------------")
