cmake_minimum_required(VERSION 3.5)

add_library(sph STATIC
    blake.c
    sph_blake.h
    sph_types.h)
set_target_properties(sph PROPERTIES C_STANDARD 99)
