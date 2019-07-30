# Try and find libsasl2.
# As soon as libsasl2 has been found, the following variables will be defined:
#
# SASL2_FOUND
# SASL2_INCLUDE_DIR
# SASL2_LIBRARIES:FILEPATH
#
# Copyright (c) 2009 Juergen Leising <jleising@users.sourceforge.net>
#
# Redistribution and use is allowed according to the terms of the New
# BSD license.
# For details see the accompanying COPYING-CMAKE-SCRIPTS file.
#

FIND_PATH(SASL2_INCLUDE_DIR NAMES sasl/sasl.h
            PATHS /include /usr/include /usr/local/include /usr/share/include /opt/include
            DOC "Try and find the header file sasl/sasl.h")

FIND_LIBRARY(SASL2_LIBRARIES NAMES sasl2
            PATHS /usr/lib /lib /usr/local/lib /usr/share/lib /opt/lib /opt/share/lib /var/lib
            DOC "Try and find libsasl2")

MARK_AS_ADVANCED(SASL2_LIBRARIES SASL2_INCLUDE_DIR)

# handle the QUIETLY and REQUIRED arguments and set SASL2_FOUND to TRUE if
# all listed variables are TRUE
INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(SASL2 REQUIRED_VARS SASL2_LIBRARIES SASL2_INCLUDE_DIR)
