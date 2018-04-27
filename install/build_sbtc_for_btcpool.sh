#!/bin/bash
# Build secp256k1 and SBTC.
# Then refactor the `src` directory of SBTC to BTCPool compatible.


# params
SRC_ROOT=$1
secp256k1_ROOT=$2
JOBS=$3


# check params
if [ "x" = "$SRC_ROOT" ] || [ "x" = "x$secp256k1_ROOT" ]; then
    echo "Usage: $0 <SBTC_SRC_ROOT> <secp256k1_ROOT> [-j<job-nums>]"
    exit 1
fi

if ! [ -d $SRC_ROOT ]; then
    echo "SRC_ROOT ($SRC_ROOT) is not a dir"
    exit 1
fi

if ! [ -d $secp256k1_ROOT ]; then
    echo "secp256k1_ROOT ($secp256k1_ROOT) is not a dir"
    exit 1
fi


# convert relative path to absolute path
SRC_ROOT=$(cd $SRC_ROOT; echo $PWD)
secp256k1_ROOT=$(cd $secp256k1_ROOT; echo $PWD)


# check SBTC version.h
version_h=$SRC_ROOT/src/framework/version.h
if ! [ -f $version_h ]; then
    echo "version.h of Super Bitcoin not exists! ($version_h)"
    exit 1
fi


# check secp256k1.c
secp256k1_c=$secp256k1_ROOT/src/secp256k1.c
if ! [ -f $secp256k1_c ]; then
    echo "secp256k1.c not exists! ($secp256k1_c)"
    exit 1
fi


# build secp256k1
cd $secp256k1_ROOT
if ! [ -f .libs/libsecp256k1.a ]; then
    ./autogen.sh
    ./configure --enable-module-recovery --enable-module-ecdh --enable-experimental
    make $JOBS
fi


# disable logging for SBTC
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

# build SBTC
cd $SRC_ROOT
if ! [ -f build/src/sbtccore/libsbtccore.a ]; then
    mkdir build 2>/dev/null
    cd build
    cmake ..
    make $JOBS
fi


# create some dirs
cd $SRC_ROOT/src
mkdir block       2>/dev/null
mkdir consensus   2>/dev/null
mkdir crypto      2>/dev/null
mkdir primitives  2>/dev/null
mkdir script      2>/dev/null
mkdir transaction 2>/dev/null


# link headers
cd $SRC_ROOT/src
ln -sf ./framework/*.h .
ln -sf ./sbtccore/*.h .
ln -sf ./wallet/*.h .
ln -sf ./utils/*.h .

cd $SRC_ROOT/src/block
ln -sf ../sbtccore/block/*.h .

cd $SRC_ROOT/src/consensus
ln -sf ../sbtccore/block/*.h .

cd $SRC_ROOT/src/crypto
ln -sf ../utils/crypto/*.h .

cd $SRC_ROOT/src/primitives
ln -sf ../sbtccore/block/*.h .

cd $SRC_ROOT/src/script
ln -sf ../sbtccore/transaction/script/*.h .

cd $SRC_ROOT/src/transaction
ln -sf ../sbtccore/transaction/*.h .


# link libraries
cd $SRC_ROOT/src
ln -sf ../build/src/framework/libframework.a ./libbitcoin_common.a
ln -sf ../build/src/sbtccore/libsbtccore.a   ./libbitcoin_consensus.a
ln -sf ../build/src/utils/libutils.a         ./libbitcoin_util.a
ln -sf ../../build/src/compat/libcompat.a    ./crypto/libbitcoin_crypto.a


# link sub-projects
cd $SRC_ROOT/src
ln -sf $secp256k1_ROOT .
