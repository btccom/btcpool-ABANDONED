#
# Dockerfile
#
# @author yihao.peng@bitmain.com
# @copyright btc.com
# @since 2019-10-08
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
    libconfig++-dev \
    libgoogle-glog-dev \
    libtool \
    libmariadbclient-dev \
    libssl-dev \
    pkg-config \
    wget \
    && apt-get autoremove -y && apt-get clean && rm -rf /var/lib/apt/lists/*

# Build libevent static library
RUN cd /tmp && \
    wget https://github.com/libevent/libevent/releases/download/release-2.1.10-stable/libevent-2.1.10-stable.tar.gz && \
    [ $(sha256sum libevent-2.1.10-stable.tar.gz | cut -d " " -f 1) = "e864af41a336bb11dab1a23f32993afe963c1f69618bd9292b89ecf6904845b0" ] && \
    tar zxf libevent-2.1.10-stable.tar.gz && \
    cd libevent-2.1.10-stable && \
    ./autogen.sh && \
    ./configure --disable-shared && \
    make -j${BUILD_JOBS} && \
    make install && \
    rm -rf /tmp/*

COPY . /work/btcpool

RUN mkdir /work/build && cd /work/build && \
    cmake -DPOOL__GENERATE_DEB_PACKAGE=ON /work/btcpool/tools/stratum_log_proxy && \
    make package -j$BUILD_JOBS

######### Release image #########
FROM ubuntu:18.04
LABEL maintainer="Yihao Peng <yihao.peng@bitmain.com>"

ARG APT_MIRROR_URL

COPY docker/btcpool/deploy/update_apt_sources.sh docker/btcpool/deploy/wait-for-it.sh /tmp/
RUN /tmp/update_apt_sources.sh

# Copy deb packages and scripts
COPY --from=build /work/build/*.deb /work/package/

# Install utilities & btcpool w/ debug symbols
RUN apt-get update  \
    && apt-get install -y /work/package/*.deb \
    && apt-get autoremove -y \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

ENTRYPOINT ["stratum_log_proxy"]
