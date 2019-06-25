cmake_minimum_required (VERSION 3.5)

project (BTCPOOL)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")

include(CTest)
include(GoogleTest)

###################################### Options ######################################
message("") # empty line
message("------------------------------- Options -------------------------------")

# Package name postfix will be different with some optional feature enabled.
# This option can be defined to change the default package name.
if(NOT POOL__DEB_PACKNAME_POSTFIX)
    set(POOL__DEB_PACKNAME_POSTFIX "")
endif()

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

# set C++ standard
set(CMAKE_CXX_STANDARD 14)
# stop building after the first error
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wdelete-non-virtual-dtor -Wfatal-errors")

# The number of concurrent jobs when compiling a third-party library
if(JOBS)
  message("-- Concurrent Jobs: ${JOBS} (-DJOBS=${JOBS})")
  set(MAKE_JOBS "-j${JOBS}") 
else()
  message("-- Concurrent Jobs: 1 (-DJOBS=1)")
  set(MAKE_JOBS "") 
endif()

option(USE_CUDA "Use cuda in build" ON)
# Use Nvidia CUDA in build
if(USE_CUDA)
  message("-- Use Nvidia CUDA in build: Enabled (-DUSE_CUDA=ON)")
else()
  message("-- Use Nvidia CUDA in build: Disabled (-DUSE_CUDA=OFF)")
  message("    WARNING: Bytom share checking will be too slow without Nvidia CUDA.")
endif()

#
# Chain Type
#
set(SUPPORTED_CHAIN_TYPES "BTC|BCH|BSV|UBTC|SBTC|LTC|ZEC")
if(NOT CHAIN_TYPE)
  message(FATAL_ERROR "Chain type not defined! (-DCHAIN_TYPE=${SUPPORTED_CHAIN_TYPES})")
endif()

# Bitcoin
if (CHAIN_TYPE STREQUAL "BTC")
  set(CHAIN_NAME "Bitcoin Core")
  set(POOL__DEB_PACKNAME_POSTFIX "${POOL__DEB_PACKNAME_POSTFIX}-bitcoin")
  set(POOL__DEFAULT_INSTALL_PREFIX "/work/btcpool.btc")

  if (HAVE_KEY_IO_H)
    add_definitions(-DINCLUDE_BTC_KEY_IO_H)
  endif()

# BitcoinCash
elseif(CHAIN_TYPE STREQUAL "BCH")
  set(CHAIN_NAME "Bitcoin ABC")
  set(POOL__DEB_PACKNAME_POSTFIX "${POOL__DEB_PACKNAME_POSTFIX}-bitcoincash")
  set(POOL__DEFAULT_INSTALL_PREFIX "/work/btcpool.bch")

# BitcoinSV
elseif(CHAIN_TYPE STREQUAL "BSV")
  set(CHAIN_NAME "Bitcoin SV")
  set(POOL__DEB_PACKNAME_POSTFIX "${POOL__DEB_PACKNAME_POSTFIX}-bitcoinsv")
  set(POOL__DEFAULT_INSTALL_PREFIX "/work/btcpool.bsv")

# UnitiedBitcoin
elseif(CHAIN_TYPE STREQUAL "UBTC")
  set(CHAIN_NAME "United Bitcoin")
  set(POOL__DEB_PACKNAME_POSTFIX "${POOL__DEB_PACKNAME_POSTFIX}-unitedbitcoin")
  set(POOL__DEFAULT_INSTALL_PREFIX "/work/btcpool.ubtc")

# SuperBitcoin
elseif(CHAIN_TYPE STREQUAL "SBTC")
  set(CHAIN_NAME "Super Bitcoin")
  set(POOL__DEB_PACKNAME_POSTFIX "${POOL__DEB_PACKNAME_POSTFIX}-superbitcoin")
  set(POOL__DEFAULT_INSTALL_PREFIX "/work/btcpool.sbtc")

# Litecoin
elseif(CHAIN_TYPE STREQUAL "LTC")
  set(CHAIN_NAME "Litecoin")
  set(POOL__DEB_PACKNAME_POSTFIX "${POOL__DEB_PACKNAME_POSTFIX}-litecoin")
  set(POOL__DEFAULT_INSTALL_PREFIX "/work/btcpool.ltc")

# ZCash
elseif(CHAIN_TYPE STREQUAL "ZEC")
  set(CHAIN_NAME "ZCash")
  set(POOL__DEB_PACKNAME_POSTFIX "${POOL__DEB_PACKNAME_POSTFIX}-zcash")
  set(POOL__DEFAULT_INSTALL_PREFIX "/work/btcpool.zec")

  add_definitions(-DINCLUDE_BTC_KEY_IO_H)

# Unknown
else()
  message("Unknown chain type! (-DCHAIN_TYPE=${CHAIN_TYPE})")
  message(FATAL_ERROR "Supported chain type: ${SUPPORTED_CHAIN_TYPES}")
endif()

message("-- Chain Type: ${CHAIN_NAME} (-DCHAIN_TYPE=${CHAIN_TYPE})")

# add chain type definitions
add_definitions(-DCHAIN_TYPE_${CHAIN_TYPE})
add_definitions(-DCHAIN_TYPE_STR="${CHAIN_TYPE}")

# add a macro that needed by chain's source code
add_definitions(-DHAVE_CONFIG_H)


#
# Whether to enable the wallet when building the blockchain code
#

option(CHAIN_ENABLE_WALLET "Enable wallet when building chain code" OFF)
if(CHAIN_ENABLE_WALLET)
  message("-- Wallet of Chain Code Enabled (-DCHAIN_ENABLE_WALLET=ON)")
else()
  message("-- Wallet of Chain Code Disabled (-DCHAIN_ENABLE_WALLET=OFF)")
endif()


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
# Rebuild Bytom's Shared Library
###
option(REBUILD_BH_SHARED "Rebuild Bytom's Shared Library" OFF)

if(REBUILD_BH_SHARED)
  message("-- Rebuild Bytom's Shared Library: Enabled (-DREBUILD_BH_SHARED=ON)")
  message("    golang required.")
  message("    https://github.com/Bytom/bytom need to be cloned to $GOPATH/src")
  message("    If you want to update the prebuild bh_shared.a/bh_shared.h,")
  message("    you can copy them from ${CMAKE_BINARY_DIR}/src/bytom to ${PROJECT_SOURCE_DIR}/prebuild/bytom")

  set(BH_SHARED_INCLUDE_DIR ${CMAKE_BINARY_DIR}/src)
  set(BH_SHARED_LIBRARY ${BH_SHARED_INCLUDE_DIR}/bytom/bh_shared.a)
else()
  message("-- Rebuild Bytom's Shared Library: Disabled (-DREBUILD_BH_SHARED=OFF)")
  message("    The prebuild bh_shared.a/bh_shared.h in ${PROJECT_SOURCE_DIR}/prebuild/bytom will be used")

  set(BH_SHARED_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/prebuild)
  set(BH_SHARED_LIBRARY ${BH_SHARED_INCLUDE_DIR}/bytom/bh_shared.a)
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
if(CHAIN_TYPE STREQUAL "BCH")
  set(BITCOIN_LIBRARIES ${BITCOIN_LIBRARIES}
    "${CHAIN_SRC_ROOT}/src/libbitcoin_common.a"
    "${CHAIN_SRC_ROOT}/src/libbitcoin_consensus.a"
    "${CHAIN_SRC_ROOT}/src/libbitcoin_util.a"
    "${CHAIN_SRC_ROOT}/src/crypto/libbitcoin_crypto_base.a"
    "${CHAIN_SRC_ROOT}/src/crypto/libbitcoin_crypto_shani.a"
    "${CHAIN_SRC_ROOT}/src/crypto/libbitcoin_crypto_avx2.a"
    "${CHAIN_SRC_ROOT}/src/crypto/libbitcoin_crypto_sse41.a")

elseif(CHAIN_TYPE STREQUAL "ZEC")
  execute_process(
      COMMAND "${CHAIN_SRC_ROOT}/depends/config.guess"
      OUTPUT_VARIABLE ZCASH_PLATFORM
      OUTPUT_STRIP_TRAILING_WHITESPACE)
  set(BITCOIN_LIBRARIES
    "${CHAIN_SRC_ROOT}/src/libbitcoin_common.a"
    "${CHAIN_SRC_ROOT}/src/libbitcoin_util.a"
    "${CHAIN_SRC_ROOT}/src/libzcash.a"
    "${CHAIN_SRC_ROOT}/src/crypto/libbitcoin_crypto.a"
    "${CHAIN_SRC_ROOT}/src/snark/libsnark.a"
    "${CHAIN_SRC_ROOT}/depends/${ZCASH_PLATFORM}/lib/libsodium.a"
    "${CHAIN_SRC_ROOT}/depends/${ZCASH_PLATFORM}/lib/librustzcash.a")

else()
  set(BITCOIN_LIBRARIES ${BITCOIN_LIBRARIES}
    "${CHAIN_SRC_ROOT}/src/libbitcoin_common.a"
    "${CHAIN_SRC_ROOT}/src/libbitcoin_consensus.a"
    "${CHAIN_SRC_ROOT}/src/libbitcoin_util.a"
    "${CHAIN_SRC_ROOT}/src/crypto/libbitcoin_crypto.a")
endif()

list(GET BITCOIN_LIBRARIES 0 BITCOIN_LIBRARIE)
if (NOT EXISTS ${BITCOIN_LIBRARIE})
  message(STATUS "building ${CHAIN_NAME}...")

  if(CHAIN_TYPE STREQUAL "ZEC")
    execute_process(WORKING_DIRECTORY "${CHAIN_SRC_ROOT}" COMMAND ./zcutil/build.sh --disable-tests ${MAKE_JOBS})

  else()
    execute_process(WORKING_DIRECTORY "${CHAIN_SRC_ROOT}" COMMAND ./autogen.sh)
  
    if (NOT CHAIN_ENABLE_WALLET)
      execute_process(WORKING_DIRECTORY "${CHAIN_SRC_ROOT}" COMMAND ./configure --with-gui=no --disable-wallet --disable-tests --disable-bench)
    else()
      execute_process(WORKING_DIRECTORY "${CHAIN_SRC_ROOT}" COMMAND ./configure --with-gui=no --disable-tests --disable-bench)
    endif()

    execute_process(WORKING_DIRECTORY "${CHAIN_SRC_ROOT}" COMMAND make ${MAKE_JOBS})
  endif()
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


#
# other libraries
#
if(CHAIN_TYPE STREQUAL "ZEC")
  FIND_PACKAGE(OpenMP REQUIRED)
endif()

find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(LibConfig REQUIRED)
find_package(Glog REQUIRED)
find_package(libzmq REQUIRED)
find_package(CURL REQUIRED)
find_package(LibEvent REQUIRED)
find_package(Protobuf REQUIRED)
find_package(Boost 1.47.0 COMPONENTS thread filesystem system regex program_options chrono REQUIRED)
find_package(ZookeeperC REQUIRED)
find_package(LibGMP REQUIRED)
find_package(LibHiredis REQUIRED)
find_package(LibPthread REQUIRED)
find_package(KafkaC REQUIRED)

if(USE_CUDA)
  find_package(CUDA 9.1)
  if(NOT CUDA_FOUND)
    message(WARNING "cuda 9.1 not found! USE_CUDA set to OFF")
    set(USE_CUDA OFF)
  endif()
endif()

find_package(MYSQL REQUIRED)
# Force to use discovered OPENSSL_CRYPTO_LIBRARY & OPENSSL_SSL_LIBRARY
string(REPLACE "-lcrypto" "" MYSQL_LIBRARIES ${MYSQL_LIBRARIES})
string(REPLACE "-lssl" "" MYSQL_LIBRARIES ${MYSQL_LIBRARIES})
string(STRIP ${MYSQL_LIBRARIES} MYSQL_LIBRARIES)


###################################### Building Tools ######################################

# Find binutils
message("-- CMAKE_OBJCOPY: ${CMAKE_OBJCOPY}")
if(NOT CMAKE_OBJCOPY)
  message("-- CMAKE_OBJCOPY not found, debug info will not separated from targets")
  mark_as_advanced(CMAKE_OBJCOPY)
endif()


########################## Generate version number #############################

# The base version
# The update of this version number may not be timely.
# We will get the version number from the git tag.
SET(BPOOL_VERSION_MAJOR "2")
SET(BPOOL_VERSION_MINOR "3")
SET(BPOOL_VERSION_PATCH "0")

# Get the current abbreviated commit hash of the working branch
if(EXISTS "${CMAKE_SOURCE_DIR}/.git")
  execute_process(
    COMMAND git describe --tag --long
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE BPOOL_GIT_DESCRIBE
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
endif()

if (BPOOL_GIT_DESCRIBE)
  message("-- Git describe: ${BPOOL_GIT_DESCRIBE}")

  # Split git describe likes `v2.3.2-0-g2d76329` to list
  # Remove prefix `v`
  string(REGEX REPLACE "^[^0-9]+" "" BPOOL_GIT_DESCRIBE ${BPOOL_GIT_DESCRIBE})
  # Split to list
  string(REPLACE "." ";" BPOOL_GIT_DESCRIBE_LIST ${BPOOL_GIT_DESCRIBE})

  list(GET BPOOL_GIT_DESCRIBE_LIST 0 BPOOL_VERSION_MAJOR)
  list(REMOVE_AT BPOOL_GIT_DESCRIBE_LIST 0)
  list(GET BPOOL_GIT_DESCRIBE_LIST 0 BPOOL_VERSION_MINOR)
  list(REMOVE_AT BPOOL_GIT_DESCRIBE_LIST 0)
  # The tag may be `v2.2.1.1-bitcoin-0-gb9fb89d`
  # So the BPOOL_VERSION_PATCH should be `1.1-bitcoin-0-gb9fb89d`
  # We need to merge the remaining items with `.`
  string(REPLACE ";" "." BPOOL_VERSION_PATCH "${BPOOL_GIT_DESCRIBE_LIST}")

elseif(EXISTS "${CMAKE_SOURCE_DIR}/.git")
  message("-- Git describe is empty, the base version will be used.")
  message("-- It is recommended to add a tag to the current commit.")
  execute_process(
      COMMAND git log -1 --format=%h
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      OUTPUT_VARIABLE BPOOL_GIT_COMMIT_HASH
      OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(BPOOL_GIT_COMMIT_HASH)
    set(BPOOL_VERSION_PATCH "${BPOOL_VERSION_PATCH}-${BPOOL_GIT_COMMIT_HASH}")
  endif()
endif()

set(BPOOL_VERSION_STR "${BPOOL_VERSION_MAJOR}.${BPOOL_VERSION_MINOR}.${BPOOL_VERSION_PATCH}")

message("-- version string: ${BPOOL_VERSION_STR}")

configure_file(src/config/bpool-version.h.in src/config/bpool-version.h)


# package version of cpack
SET(CPACK_PACKAGE_VERSION_MAJOR "${BPOOL_VERSION_MAJOR}")
SET(CPACK_PACKAGE_VERSION_MINOR "${BPOOL_VERSION_MINOR}")
SET(CPACK_PACKAGE_VERSION_PATCH "${BPOOL_VERSION_PATCH}")


###################################### Targets ######################################

if (REBUILD_BH_SHARED)
  message("-- rebuilding Bytom's shared library...")
  execute_process(
    COMMAND go build -buildmode=c-archive -o ${BH_SHARED_LIBRARY} ${CMAKE_SOURCE_DIR}/src/bytom/bh_shared.go
  )
endif()

add_subdirectory(3rdparty/libblake2)
add_subdirectory(3rdparty/libethash)
add_subdirectory(3rdparty/libsph)
add_subdirectory(3rdparty/rlpvalue)

if(USE_CUDA)
  set(USE_CUDA_INCLUDE_DIRECTORY ${CUDA_INCLUDE_DIRECTORIES})
  add_subdirectory(3rdparty/bytom/cutil)
  set(USE_CUDA_LIBRARIES GpuTs ${CUDA_LIBRARIES} ${CUDA_CUBLAS_LIBRARIES})
else()
  add_definitions(-DNO_CUDA)
endif(USE_CUDA)

add_definitions(-DFMT_HEADER_ONLY)

include_directories(
  ${CMAKE_CURRENT_BINARY_DIR}/src
  3rdparty
  3rdparty/equihash
  src
  test
  ${CHAIN_SRC_ROOT}/src
  ${CHAIN_SRC_ROOT}/src/config
  ${CHAIN_SRC_ROOT}/src/secp256k1/include
  ${OPENSSL_INCLUDE_DIR}
  ${Boost_INCLUDE_DIRS}
  ${LIBZMQ_INCLUDE_DIR}
  ${GLOG_INCLUDE_DIRS}
  ${KAFKA_INCLUDE_DIRS}
  ${ZLIB_INCLUDE_DIRS}
  ${LIBEVENT_INCLUDE_DIR}
  ${USE_CUDA_INCLUDE_DIRECTORY}
  ${BH_SHARED_INCLUDE_DIR}
  ${MYSQL_INCLUDE_DIR})

set(THIRD_LIBRARIES ethash blake2 sph ${BH_SHARED_LIBRARY} ${USE_CUDA_LIBRARIES}
                    ${BITCOIN_LIBRARIES} ${secp256k1_LIBRARIES} ${GLOG_LIBRARIES} ${KAFKA_LIBRARIES} ${ZLIB_LIBRARIES} ${ZOOKEEPER_LIBRARIES}
                    ${MYSQL_LIBRARIES} ${LIBZMQ_LIBRARIES} ${Hiredis_LIBRARIES} ${CURL_LIBRARIES} ${Boost_LIBRARIES} ${LIBCONFIGPP_LIBRARY}
                    ${LIBEVENT_LIB} ${LIBEVENT_PTHREADS_LIB} ${GMP_LIBRARIES} ${LIBEVENT_OPENSSL_LIB} ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY}
                    ${PTHREAD_LIBRARIES} ${PROTOBUF_LIBRARIES})

if(CHAIN_TYPE STREQUAL "ZEC")
  include_directories(${OpenMP_INCLUDE_PATH})
  set(THIRD_LIBRARIES ${THIRD_LIBRARIES} ${OpenMP_CXX_LIBRARIES})
endif()

add_library(equihash 3rdparty/equihash/crypto/equihash_impl.cpp)

add_library(beam
    3rdparty/beam/core/difficulty.cpp
    3rdparty/beam/core/ecc.cpp
    3rdparty/beam/core/uintBig.cpp
    3rdparty/beam/core/static_members.cpp
    3rdparty/beam/pow/equihash.cpp
    3rdparty/beam/utility/common.cpp)
target_compile_options(beam PRIVATE -Wno-unused-function)
target_include_directories(beam PUBLIC 3rdparty/beam)
target_link_libraries(beam PUBLIC equihash)

add_library(cuckoo
  3rdparty/cuckoo/cuckaroo.cpp
  3rdparty/cuckoo/cuckatoo.cpp
  3rdparty/cuckoo/siphash.cpp)

file(GLOB LIB_SOURCES
        src/*.cc
        src/rsk/*.cc
        src/vcash/*.cc
        src/ssl/*.cc
    )
file(GLOB LIB_SOURCES_CHAINS
        src/eth/*.cc
        src/bytom/*.cc 
        src/bitcoin/*.cc 
        src/sia/*.cc
        src/decred/*.cc
        src/beam/*.cc
        src/grin/*.cc
    )

set(LIB_SOURCES_SHARES)
foreach(SHARE_TYPE beam bitcoin bytom decred eth sia grin)
  file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/src/${SHARE_TYPE})
  add_custom_command(
      COMMAND ${PROTOBUF_PROTOC_EXECUTABLE} --cpp_out=${CMAKE_CURRENT_BINARY_DIR}/src/${SHARE_TYPE} ${SHARE_TYPE}.proto
      COMMENT "Generating ${SHARE_TYPE} share protobuf sources..."
      DEPENDS src/${SHARE_TYPE}/${SHARE_TYPE}.proto
      OUTPUT src/${SHARE_TYPE}/${SHARE_TYPE}.pb.h src/${SHARE_TYPE}/${SHARE_TYPE}.pb.cc
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/${SHARE_TYPE})
  list(APPEND LIB_SOURCES_SHARES ${CMAKE_CURRENT_BINARY_DIR}/src/${SHARE_TYPE}/${SHARE_TYPE}.pb.cc)
endforeach(SHARE_TYPE)

set(LIB_SOURCES_PROMETHEUS
    src/prometheus/Exporter.cc
)

add_library(
    btcpool STATIC
    ${LIB_SOURCES}
    ${LIB_SOURCES_PROMETHEUS}
    ${LIB_SOURCES_SHARES}
    ${LIB_SOURCES_CHAINS})
target_link_libraries(btcpool PUBLIC beam cuckoo)
target_link_libraries(btcpool PUBLIC rlpvalue)

if(BUILD_TESTING)
  file(GLOB_RECURSE TEST_SOURCES test/*.cc)
  add_executable(unittest ${TEST_SOURCES} 3rdparty/gmock-gtest-all.cc)
  target_link_libraries(unittest btcpool ${THIRD_LIBRARIES})
endif()

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

add_executable(sserver src/sserver/StratumServerMain.cc)
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

if(BUILD_TESTING)
  # testcase
  set(INSTALL_TEST_TARGETS unittest)
  gtest_discover_tests(unittest)
endif()

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

if(BUILD_TESTING)
# test targets
  foreach(tgt ${INSTALL_TEST_TARGETS})
    # binary
    install(PROGRAMS     ${CMAKE_BINARY_DIR}/${tgt}
            COMPONENT   main
            DESTINATION ${POOL__INSTALL_PREFIX}/build)
  endforeach()
endif()

# documents
install(FILES
            LICENSE
        COMPONENT
            main
        DESTINATION
            ${POOL__INSTALL_PREFIX})

# document dirss
install(DIRECTORY
            install
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

  # components
  set(CPACK_DEB_COMPONENT_INSTALL ON)
  set(CPACK_DEB_USE_DISPLAY_NAME_IN_FILENAME ON)
  set(CPACK_DEBIAN_MAIN_PACKAGE_NAME ${CPACK_PACKAGE_NAME})
  set(CPACK_DEBIAN_DBGINFO_PACKAGE_NAME ${CPACK_PACKAGE_NAME}-dbg)

  # dependencies
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

  include(CPack)
endif()
