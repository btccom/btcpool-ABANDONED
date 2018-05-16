cmake_minimum_required (VERSION 2.6)

project (BTCPOOL-GBTSYNC)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/../../cmake/Modules/")

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

include_directories(./)
include_directories(../)
include_directories(../../test)
include_directories(${GLOG_INCLUDE_DIRS})
include_directories(${MYSQL_INCLUDE})
include_directories(${PTHREAD_INCLUDE_DIR})
include_directories(${LIBCONFIGPP_INCLUDE_DIR})

file(GLOB LIB_SOURCES *.cpp)
file(GLOB MAIN_SOURCES main/*.cpp)

add_executable(gbtsync ${LIB_SOURCES} ${MAIN_SOURCES})
target_link_libraries(gbtsync ${LIBCONFIGPP_LIBRARY} ${MYSQL_LIB} ${PTHREAD_LIBRARIES} ${GLOG_LIBRARIES})

file(GLOB TEST_SOURCES ../../test/gtest/*.cc
                       ../../test/TestGbtSync/*.cpp
                       ../../test/TestMain.cc)

add_executable(gbtsynctest ${LIB_SOURCES} ${TEST_SOURCES})
target_link_libraries(gbtsynctest ${MYSQL_LIB} ${PTHREAD_LIBRARIES} ${GLOG_LIBRARIES})

file(COPY "../../test/TestGbtSync/testdata/" DESTINATION testdata)
