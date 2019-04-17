#
# Dockerfile
#
# @author hanjiang.yu@bitmain.com
# @copyright btc.com
# @since 2018-12-01
#
#
FROM ubuntu:18.04
LABEL maintainer="Hanjiang Yu <hanjiang.yu@bitmain.com>"

ARG APT_MIRROR_URL
ARG BUILD_JOBS=1

COPY update_apt_sources.sh /tmp
RUN /tmp/update_apt_sources.sh

# Install build dependencies
RUN apt-get update && apt-get install -y \
    autoconf \
    automake \
    autotools-dev \
    bsdmainutils \
    build-essential \
    cmake \
    curl \
    git \
    libboost-all-dev \
    libconfig++-dev \
    libcurl4-openssl-dev \
    libgmp-dev \
    libgoogle-glog-dev \
    libhiredis-dev \
    libmysqlclient-dev \
    libprotobuf-dev \
    libssl-dev \
    libtool \
    libzmq3-dev \
    libzookeeper-mt-dev \
    openssl \
    pkg-config \
    protobuf-compiler \
    wget \
    yasm \
    zlib1g-dev \
    && apt-get autoremove && apt-get clean q&& rm -rf /var/lib/apt/lists/*

# Build libevent static library
RUN cd /tmp && \
    wget https://github.com/libevent/libevent/releases/download/release-2.1.9-beta/libevent-2.1.9-beta.tar.gz && \
    [ $(sha256sum libevent-2.1.9-beta.tar.gz | cut -d " " -f 1) = "eeb4c6eb2c4021e22d6278cdcd02815470243ed81077be0cbd0f233fa6fc07e8" ] && \
    tar zxf libevent-2.1.9-beta.tar.gz && \
    cd libevent-2.1.9-beta && \
    ./autogen.sh && \
    ./configure --disable-shared && \
    make -j${BUILD_JOBS} && \
    make install && \
    rm -rf /tmp/*

# Build librdkafka static library
RUN cd /tmp && wget https://github.com/edenhill/librdkafka/archive/0.9.1.tar.gz && \
    [ $(sha256sum 0.9.1.tar.gz | cut -d " " -f 1) = "5ad57e0c9a4ec8121e19f13f05bacc41556489dfe8f46ff509af567fdee98d82" ] && \
    tar zxvf 0.9.1.tar.gz && cd librdkafka-0.9.1 && \
    ./configure && make && make install && rm -rf /tmp/*

# Remove dynamic libraries of librdkafka
# In this way, the constructed deb package will
# not have dependencies that not from software sources.
RUN cd /usr/local/lib && \
    find . | grep 'rdkafka' | grep '.so' | xargs rm

# Build blockchain
RUN mkdir /work && git clone https://github.com/superbitcoin/SuperBitcoin.git --branch v0.16.2 --depth 1 /work/blockchain && \
    cd /work/blockchain && ./autogen.sh && ./configure --with-gui=no --disable-wallet --disable-tests --disable-bench && make && \
    cd /work/blockchain/src/secp256k1 && ./autogen.sh && ./configure --enable-module-recovery && make

# For forward compatible
RUN ln -s /work/blockchain /work/bitcoin

# Used later by btcpool build
ENV CHAIN_TYPE=SBTC
