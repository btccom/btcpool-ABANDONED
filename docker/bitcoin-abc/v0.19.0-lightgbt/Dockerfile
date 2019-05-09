#
# Dockerfile
#
# @author zhibiao.pan@bitmain.com, yihao.peng@bitmain.com
# @copyright btc.com
# @since 2016-08-01
#
#
FROM phusion/baseimage:0.11
MAINTAINER YihaoPeng <yihao.peng@bitmain.com>

CMD ["/sbin/my_init"]

# use aliyun source
# ADD sources-aliyun.com.list /etc/apt/sources.list

RUN apt-get update && apt-get install -y \
    autoconf \
    automake \
    bsdmainutils \
    build-essential \
    curl \
    git \
    libboost-all-dev \
    libevent-dev \
    libssl-dev \
    libtool \
    libzmq3-dev \
    pkg-config \
    unzip \
    wget \
    yasm \
    && apt-get autoremove && apt-get clean && rm -rf /var/lib/apt/lists/*

# build bitcoind
RUN cd /tmp && git clone -b v0.19.0_lightgbt --depth 1 https://github.com/btccom/bitcoin-abc-1.git && \
    cd bitcoin-abc-1 && ./autogen.sh && ./configure --disable-bench --disable-wallet --disable-tests && \
    make -j$(nproc) && make install && rm -r /tmp/bitcoin-abc-1

# mkdir bitcoind data dir
RUN mkdir -p /root/.bitcoin

# logrotate
ADD logrotate-bitcoind /etc/logrotate.d/bitcoind

#
# services
#

# service for mainnet
RUN mkdir    /etc/service/bitcoind
ADD run      /etc/service/bitcoind/run
RUN chmod +x /etc/service/bitcoind/run

#service for testnet
#RUN mkdir        /etc/service/bitcoind_testnet
#ADD run_testnet /etc/service/bitcoind_testnet/run
#RUN chmod +x     /etc/service/bitcoind_testnet/run
