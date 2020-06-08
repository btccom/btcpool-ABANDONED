#
# Dockerfile
#
# @author yihao.peng@bitmain.com
# @copyright btc.com
# @since 2020-06-09
#
#
FROM ubuntu:18.04 as build
LABEL maintainer="Yihao Peng <yihao.peng@bitmain.com>"

ARG APT_MIRROR_URL
ARG BUILD_JOBS=1

COPY docker/btcpool/deploy/update_apt_sources.sh /tmp/
RUN /tmp/update_apt_sources.sh

# Install build dependencies
RUN apt-get update && apt-get install -y \
    autoconf \
    automake \
    build-essential \
    cmake \
    libboost-all-dev \
    libconfig++-dev \
    libgoogle-glog-dev \
    libssl-dev \
    libtool \
    libzookeeper-mt-dev \
    libsasl2-dev \
    libmysqlclient-dev \
    pkg-config \
    python \
    wget \
    zlib1g-dev \
    && apt-get autoremove && apt-get clean q&& rm -rf /var/lib/apt/lists/*

# Build librdkafka static library
# Remove dynamic libraries of librdkafka
# In this way, the constructed deb package will
# not have dependencies that not from software sources.
RUN cd /tmp && wget https://github.com/edenhill/librdkafka/archive/v1.1.0.tar.gz && \
    [ $(sha256sum v1.1.0.tar.gz | cut -d " " -f 1) = "123b47404c16bcde194b4bd1221c21fdce832ad12912bd8074f88f64b2b86f2b" ] && \
    tar zxf v1.1.0.tar.gz && cd librdkafka-1.1.0 && \
    ./configure && make && make install && rm -rf /tmp/* && \
    cd /usr/local/lib && \
    find . | grep 'rdkafka' | grep '.so' | xargs rm

COPY . /work/btcpool

RUN mkdir /work/build && cd /work/build && \
    cmake -DPOOL__GENERATE_DEB_PACKAGE=ON /work/btcpool/tools/worker_update && \
    make package -j$BUILD_JOBS

######### Release image #########
FROM ubuntu:18.04
LABEL maintainer="Yihao Peng <yihao.peng@bitmain.com>"

ARG APT_MIRROR_URL

COPY docker/btcpool/deploy/update_apt_sources.sh /tmp/
RUN /tmp/update_apt_sources.sh

# Copy deb packages and scripts
COPY --from=build /work/build/*.deb /work/package/
COPY --from=build /work/btcpool/install/cfg-generator /work/cfg-generator
COPY tools/worker_update/entrypoint.sh /

# Install utilities & btcpool w/ debug symbols
RUN apt-get update && \
    apt-get install -y php-cli /work/package/*.deb && \
    apt-get autoremove && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

ENTRYPOINT ["/entrypoint.sh"]
