#
# Dockerfile
#
# @author hanjiang.yu@bitmain.com
# @copyright btc.com
# @since 2018-12-01
#
#

######### Build image #########
FROM ubuntu:18.04 as build
LABEL maintainer="Hanjiang Yu <hanjiang.yu@bitmain.com>"

ARG BUILD_JOBS=1
ARG BUILD_TYPE
ARG CHAIN_TYPE
ARG CHAIN_URL
ARG CHAIN_TAG
ARG USER_DEFINED_COINBASE
ARG USER_DEFINED_COINBASE_SIZE
ARG WORK_WITH_STRATUM_SWITCHER

# Install build dependencies
RUN apt-get update && apt-get install -y \
    autoconf \
    automake \
    autotools-dev \
    bsdmainutils \
    build-essential \
    cmake \
    git \
    libboost-all-dev \
    libconfig++-dev \
    libcurl4-openssl-dev \
    libgmp-dev \
    libgoogle-glog-dev \
    libhiredis-dev \
    libmysqlclient-dev \
    libssl-dev \
    libtool \
    libzmq3-dev \
    libzookeeper-mt-dev \
    openssl \
    pkg-config \
    wget \
    yasm \
    zlib1g-dev \
    && apt-get autoremove

# Build libevent static library
#
# Notice: the release of libevent has a dead lock bug,
#         so use the code for the master branch here.
# Issue:  sserver accidental deadlock when release StratumSession
#         from consume thread
#         <https://github.com/btccom/btcpool/issues/75>
RUN cd /tmp && \
    git clone https://github.com/libevent/libevent.git --depth 1 && \
    cd libevent && \
    ./autogen.sh && \
    ./configure --disable-shared && \
    make -j${BUILD_JOBS} && \
    make install

# Build librdkafka static library
RUN cd /tmp && wget https://github.com/edenhill/librdkafka/archive/0.9.1.tar.gz && \
    [ $(sha256sum 0.9.1.tar.gz | cut -d " " -f 1) = "5ad57e0c9a4ec8121e19f13f05bacc41556489dfe8f46ff509af567fdee98d82" ] && \
    tar zxvf 0.9.1.tar.gz && cd librdkafka-0.9.1 && \
    ./configure && make -j${BUILD_JOBS} && make install

# Checkout blockchain source (if exists)
RUN [ -z ${CHAIN_URL} ] || git clone --branch ${CHAIN_TAG} --depth 1 ${CHAIN_URL} /tmp/bitcoin

# Copy & build btcpool
COPY . /tmp/btcpool
RUN mkdir -p /tmp/build && cd /tmp/build && cmake \
    -DCHAIN_SRC_ROOT=/tmp/bitcoin \
    -DCHAIN_TYPE=${CHAIN_TYPE} \
    -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
    -DJOBS=${BUILD_JOBS} \
    -DKAFKA_LINK_STATIC=ON \
    -DPOOL__GENERATE_DEB_PACKAGE=ON \
    -DPOOL__INSTALL_PREFIX=/work/btcpool \
    -DPOOL__USER_DEFINED_COINBASE_SIZE=${USER_DEFINED_COINBASE_SIZE} \
    -DPOOL__USER_DEFINED_COINBASE=${USER_DEFINED_COINBASE} \
    -DPOOL__WORK_WITH_STRATUM_SWITCHER=${WORK_WITH_STRATUM_SWITCHER} \
    /tmp/btcpool
RUN cd /tmp/build && make -j${BUILD_JOBS}
RUN cd /tmp/build && make package && mkdir -p /work/package && cp *.deb /work/package/

######### Release image #########
FROM ubuntu:18.04
LABEL maintainer="Hanjiang Yu <hanjiang.yu@bitmain.com>"

# Copy deb packages
COPY --from=build /work/package /work/package

# Install deb packages
RUN apt-get update && apt-get install -y /work/package/*-main.deb
    
