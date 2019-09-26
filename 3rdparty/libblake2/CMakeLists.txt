cmake_minimum_required(VERSION 3.5)

file(GLOB FILES ./*.c)

add_library(blake2 STATIC ${FILES})
set_target_properties(blake2 PROPERTIES C_STANDARD 99)

# End users do not need to install static libraries and header files
#install(TARGETS blake2 DESTINATION /usr/local/lib)
#install(FILES blake2.h DESTINATION /usr/local/include)
