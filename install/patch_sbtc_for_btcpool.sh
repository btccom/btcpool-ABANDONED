#!/bin/bash
# Patch SBTC source code for BTCPool compatible.


# params
SRC_ROOT=$1


# check params
if [ "x" = "x$SRC_ROOT" ]; then
    echo "Usage: $0 <SBTC_SRC_ROOT>"
    exit 1
fi

if ! [ -d $SRC_ROOT ]; then
    echo "SRC_ROOT ($SRC_ROOT) is not a dir"
    exit 1
fi


# convert relative path to absolute path
SRC_ROOT=$(cd $SRC_ROOT; echo $PWD)


# check SBTC version.h
version_h=$SRC_ROOT/src/framework/version.h
if ! [ -f $version_h ]; then
    echo "version.h of Super Bitcoin not exists! ($version_h)"
    exit 1
fi


# disable logging
logger_h=$SRC_ROOT/src/utils/logger.h
if ! [ -f $logger_h ]; then
    echo "$logger_h not exists!"
    exit 1
fi
if ! [ -f $logger_h.ori ]; then
    cp $logger_h $logger_h.ori
    sed -i 's/#define ENABLE_LOGGING/#undef ENABLE_LOGGING/g' $logger_h
    sed -i 's/#define LOG_SRC_LOC_INFO/#undef LOG_SRC_LOC_INFO/g' $logger_h
    echo "" >> $logger_h # empty line
    echo "" >> $logger_h # empty line
    echo "#undef VMLog" >> $logger_h
    echo "#define VMLog(a...)"  >> $logger_h
fi


# disable assert checking
cd $SRC_ROOT/src
disable_assert_files="./p2p/net_processing.cpp ./sbtccore/block/validation.cpp"
for f in $disable_assert_files
do
    if ! [ -f $f.ori ]; then
        cp $f $f.ori
        sed -i 's@# error@//# error@g' $f
    fi
done


# disable unused building target
cd $SRC_ROOT/src
cmake_file="./CMakeLists.txt"
if ! [ -f $cmake_file.ori ]; then
    cp $cmake_file $cmake_file.ori
    sed -i 's/add_subdirectory(sbtcd)/#add_subdirectory(sbtcd)/g' $cmake_file
    sed -i 's/add_subdirectory(sbtc-tx)/#add_subdirectory(sbtc-tx)/g' $cmake_file
    sed -i 's/add_subdirectory(sbtc-cli)/#add_subdirectory(sbtc-cli)/g' $cmake_file
fi


# link headers
cd $SRC_ROOT/src
ln -sf ./sbtccore/block ./consensus
ln -sf ./sbtccore/block ./primitives
