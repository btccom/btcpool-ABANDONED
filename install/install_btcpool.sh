#!/bin/bash
#
# init install script for btcpool
#
# OS: Ubuntu 14.04 LTS
# 
# @copyright btc.com
# @author Kevin Pan
# @since 2016-08
#

CPUS=`lscpu | grep '^CPU(s):' | awk '{print $2}'`

apt-get update
apt-get install -y build-essential autotools-dev libtool autoconf automake pkg-config cmake
apt-get install -y openssl libssl-dev libcurl4-openssl-dev libconfig++-dev libboost-all-dev libmysqlclient-dev

# zmq-v4.1.5
mkdir -p /root/source && cd /root/source
wget https://github.com/zeromq/zeromq4-1/releases/download/v4.1.5/zeromq-4.1.5.tar.gz
tar zxvf zeromq-4.1.5.tar.gz
cd zeromq-4.1.5
./autogen.sh && ./configure && make -j $CPUS
make check && make install && ldconfig

# glog-v0.3.4
mkdir -p /root/source && cd /root/source
wget https://github.com/google/glog/archive/v0.3.4.tar.gz
tar zxvf v0.3.4.tar.gz
cd glog-0.3.4
./configure && make -j $CPUS && make install

# librdkafka-v0.9.1
apt-get install -y zlib1g zlib1g-dev
mkdir -p /root/source && cd /root/source
wget https://github.com/edenhill/librdkafka/archive/0.9.1.tar.gz
tar zxvf 0.9.1.tar.gz
cd librdkafka-0.9.1
./configure && make -j $CPUS && make install

# libevent-2.0.22-stable
mkdir -p /root/source && cd /root/source
wget https://github.com/libevent/libevent/releases/download/release-2.0.22-stable/libevent-2.0.22-stable.tar.gz
tar zxvf libevent-2.0.22-stable.tar.gz
cd libevent-2.0.22-stable
./configure
make -j $CPUS
make install

# btcpool
mkdir -p /work && cd /work
git clone https://github.com/btccom/btcpool.git
cd /work/btcpool
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j $CPUS

