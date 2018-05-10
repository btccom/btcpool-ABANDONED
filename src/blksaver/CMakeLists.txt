cmake_minimum_required (VERSION 2.6)

file(GLOB SOURCES *.cpp)

include_directories(/usr/include/mysql/)
add_executable(blocksaver ${SOURCES})
target_link_libraries(blocksaver pthread mysqlclient config++ )


include_directories(./)
include_directories(test)

get_filename_component(full_path_blocksavermain_cpp ${CMAKE_CURRENT_SOURCE_DIR}/blocksavermain.cpp ABSOLUTE)
list(REMOVE_ITEM SOURCES ${full_path_blocksavermain_cpp})
file(GLOB TEST_SOURCES test/*.cpp)
set(TEST_SOURCES ${SOURCES} test/gtest/gtest-all.cc ${TEST_SOURCES})

add_executable(blocksavertest ${TEST_SOURCES})
target_link_libraries(blocksavertest pthread mysqlclient)