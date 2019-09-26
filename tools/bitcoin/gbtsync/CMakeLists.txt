cmake_minimum_required (VERSION 3.5)

project (BTCPOOL-GBTSYNC)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/../../../cmake/Modules/")

################### options ###################

#
# Build Type: -DCMAKE_BUILD_TYPE=Debug|Release
# Default: Release
#
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
message("-- Build Type: ${CMAKE_BUILD_TYPE} (-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE})")
IF(CMAKE_BUILD_TYPE MATCHES Debug)
  set(CMAKE_CXX_FLAGS "-g -O0 -Wall -Wno-unknown-pragmas")
  set(CMAKE_C_FLAGS   "-g -O0 -Wall -Wno-unknown-pragmas")
ELSEIF(CMAKE_BUILD_TYPE MATCHES Release)
  set(CMAKE_CXX_FLAGS "-g -O2 -Wall -Wno-unknown-pragmas")
  set(CMAKE_C_FLAGS   "-g -O2 -Wall -Wno-unknown-pragmas")
ELSE()
  set(CMAKE_CXX_FLAGS "-g -Wall -Wno-unknown-pragmas")
  set(CMAKE_C_FLAGS   "-g -Wall -Wno-unknown-pragmas")
ENDIF()

# stop building after the first error
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fmax-errors=1")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}   -fmax-errors=1")

# set C++ standard
set(CMAKE_CXX_STANDARD 11)

################### libraries ###################

# glog
find_package(Glog)
if(NOT GLOG_FOUND)
message(FATAL_ERROR "Glog not found!")
endif()

# mysql
find_package(MYSQL)
if(NOT MYSQL_FOUND)
  message(FATAL_ERROR "libmysqlclient not found!")
endif()
# Force to use discovered OPENSSL_CRYPTO_LIBRARY & OPENSSL_SSL_LIBRARY
string(REPLACE "-lcrypto" "" MYSQL_LIBRARIES ${MYSQL_LIBRARIES})
string(REPLACE "-lssl" "" MYSQL_LIBRARIES ${MYSQL_LIBRARIES})
string(STRIP ${MYSQL_LIBRARIES} MYSQL_LIBRARIES)

# pthread
find_package(LibPthread)
if(NOT PTHREAD_FOUND)
  message(FATAL_ERROR "libpthread not found!")
endif()

# libconfig
find_package(LibConfig)
if(NOT LIBCONFIG_FOUND)
  message(FATAL_ERROR "LibConfig not found!")
endif()

################### targets ###################

add_definitions(-DCHAIN_TYPE_BCH)
add_definitions(-DCHAIN_TYPE_STR="BCH")
add_subdirectory(../../../src/config config)

include_directories(./)
include_directories(../)
include_directories(../../../src)
include_directories(../../../test)
include_directories(${GLOG_INCLUDE_DIRS})
include_directories(${MYSQL_INCLUDE_DIR})
include_directories(${PTHREAD_INCLUDE_DIR})
include_directories(${LIBCONFIGPP_INCLUDE_DIR})

file(GLOB LIB_SOURCES *.cpp)
file(GLOB MAIN_SOURCES main/*.cpp)

add_executable(gbtsync ${LIB_SOURCES} ${MAIN_SOURCES})
target_link_libraries(gbtsync ${LIBCONFIGPP_LIBRARY} ${MYSQL_LIBRARIES} ${PTHREAD_LIBRARIES} ${GLOG_LIBRARIES})

file(GLOB TEST_SOURCES ../../../test/gmock-gtest-all.cc
                       ../../../test/TestMain.cc
                       ./TestGbtSync/*.cpp)

add_executable(gbtsynctest ${LIB_SOURCES} ${TEST_SOURCES})
target_link_libraries(gbtsynctest ${MYSQL_LIBRARIES} ${PTHREAD_LIBRARIES} ${GLOG_LIBRARIES})

file(COPY "./TestGbtSync/testdata/" DESTINATION testdata)

install(TARGETS gbtsync
DESTINATION bin)
