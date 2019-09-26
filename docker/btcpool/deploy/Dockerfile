#
# Dockerfile
#
# @author hanjiang.yu@bitmain.com
# @copyright btc.com
# @since 2018-12-01
#
#

######### Build image #########
ARG BASE_IMAGE
FROM ${BASE_IMAGE} as build
LABEL maintainer="Hanjiang Yu <hanjiang.yu@bitmain.com>"

ARG GIT_DESCRIBE=""
ARG BUILD_JOBS=1
ARG BUILD_TESTING=ON
ARG BUILD_TYPE=Release
ARG USER_DEFINED_COINBASE=OFF
ARG USER_DEFINED_COINBASE_SIZE=10
ARG WORK_WITH_STRATUM_SWITCHER=OFF

# Copy & build btcpool
COPY . /tmp/btcpool
RUN mkdir -p /tmp/build && cd /tmp/build && cmake \
    -DBPOOL_GIT_DESCRIBE=${GIT_DESCRIBE} \
    -DBUILD_TESTING=${BUILD_TESTING} \
    -DCHAIN_SRC_ROOT=/work/blockchain \
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
RUN cd /tmp/build && make package -j${BUILD_JOBS} && mkdir -p /work/package && cp *.deb /work/package/

######### Release image #########
FROM ubuntu:18.04
LABEL maintainer="Hanjiang Yu <hanjiang.yu@bitmain.com>"

ARG APT_MIRROR_URL

COPY docker/btcpool/deploy/update_apt_sources.sh /tmp
RUN /tmp/update_apt_sources.sh

# Copy deb packages
COPY --from=build /work/package /work/package

# Install utilities & btcpool w/ debug symbols
RUN apt-get update && \
    apt-get install -y \
    busybox \
    curl \
    htop \
    less \
    net-tools \
    tcpdump \
    tcpflow \
    telnet \
    tmux && \
    busybox --install -s && \
    apt-get install -y /work/package/*-main.deb && \
    apt-get autoremove && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# startup scripts
COPY ./docker/btcpool/deploy/entrypoint.sh ./docker/btcpool/deploy/wait-for-it.sh /

# entrypoint
ENTRYPOINT ["/entrypoint.sh"]
