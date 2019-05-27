cmake_minimum_required(VERSION 3.5)

add_library(rlpvalue STATIC
    include/rlpvalue.h
    lib/rlpvalue.cpp
    lib/rlpvalue_get.cpp
    lib/rlpvalue_read.cpp
    lib/rlpvalue_utffilter.h
    lib/rlpvalue_write.cpp)
target_include_directories(rlpvalue
    PUBLIC include)
