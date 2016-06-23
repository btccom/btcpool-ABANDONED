#
# Copyright 2013 Produban
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# Tries to find Kafka C Client headers and libraries.
#
# Usage of this module as follows:
#
#  find_package(KafkaC)
#
# Variables used by this module, they can change the default behaviour and need
# to be set before calling find_package:
#
#  KAFKA_ROOT_DIR  Set this variable to the root installation of
#                  Kafka Library if the module has problems finding
#                  the proper installation path.
#
# Variables defined by this module:
#
#  KAFKA_FOUND              System has Kafka libs/headers
#  KAFKA_LIBRARIES          The Kafka libraries
#  KAFKA_INCLUDE_DIRS       The location of Kafka headers

message ("\nLooking for kafka C headers and libraries")

if (KAFKA_ROOT_DIR)
    message (STATUS "Root dir: ${KAFKA_ROOT_DIR}")
endif ()

find_path(KAFKA_INCLUDE_DIR 
	NAMES
		librdkafka/rdkafka.h
    PATHS 
		${KAFKA_ROOT_DIR}/include
    PATH_SUFFIXES 
        librdkafka
)

if (KAFKA_LINK_STATIC) 
    set (KAFKA_LOOK_FOR_LIB_NAMES librdkafka.a rdkafka)
else ()
    set (KAFKA_LOOK_FOR_LIB_NAMES rdkafka)
endif ()

find_library(KAFKA_LIBRARY
    NAMES 
		${KAFKA_LOOK_FOR_LIB_NAMES}
    PATHS 
		${KAFKA_ROOT_DIR}/lib 
)
			 
include(FindPackageHandleStandardArgs)

# handle the QUIETLY and REQUIRED arguments and set KAFKA_FOUND to TRUE
# if all listed variables are TRUE
find_package_handle_standard_args(Kafka 
	DEFAULT_MSG
    KAFKA_LIBRARY 
	KAFKA_INCLUDE_DIR
)

mark_as_advanced(KAFKA_INCLUDE_DIR KAFKA_LIBRARY)

if (KAFKA_FOUND)
	set(KAFKA_INCLUDE_DIRS ${KAFKA_INCLUDE_DIR})
	set(KAFKA_LIBRARIES ${KAFKA_LIBRARY})
	
    get_filename_component (KAFKA_LIBRARY_DIR ${KAFKA_LIBRARY} PATH)
    get_filename_component (KAFKA_LIBRARY_NAME ${KAFKA_LIBRARY} NAME_WE)
    
    mark_as_advanced (KAFKA_LIBRARY_DIR KAFKA_LIBRARY_NAME)
    
	message (STATUS "Include directories: ${KAFKA_INCLUDE_DIRS}") 
	message (STATUS "Libraries: ${KAFKA_LIBRARIES}") 
endif ()
