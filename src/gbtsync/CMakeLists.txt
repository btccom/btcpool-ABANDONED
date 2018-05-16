cmake_minimum_required (VERSION 2.6)

file(GLOB SOURCES *.cpp)

include_directories(/usr/include/mysql/)
add_executable(gbtsync ${SOURCES})
target_link_libraries(gbtsync pthread mysqlclient config++ )


include_directories(./)
include_directories(test)

get_filename_component(full_path_gbtsyncmain_cpp ${CMAKE_CURRENT_SOURCE_DIR}/gbtsyncmain.cpp ABSOLUTE)
list(REMOVE_ITEM SOURCES ${full_path_gbtsyncmain_cpp})
file(GLOB TEST_SOURCES test/*.cpp)
set(TEST_SOURCES ${SOURCES} test/gtest/gtest-all.cc ${TEST_SOURCES})

add_executable(gbtsynctest ${TEST_SOURCES})
target_link_libraries(gbtsynctest pthread mysqlclient)