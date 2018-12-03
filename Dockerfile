#
# Dockerfile
#
# @author hanjiang.yu@bitmain.com
# @copyright btc.com
# @since 2018-12-01
#
#

######### Build image #########
ARG BUILD_ENV_TAG
FROM btccom/btcpool_build:${BUILD_ENV_TAG} as build
LABEL maintainer="Hanjiang Yu <hanjiang.yu@bitmain.com>"

ARG BUILD_JOBS=1
ARG BUILD_TYPE
ARG USER_DEFINED_COINBASE
ARG USER_DEFINED_COINBASE_SIZE
ARG WORK_WITH_STRATUM_SWITCHER

# Copy & build btcpool
COPY . /tmp/btcpool
RUN mkdir -p /tmp/build && cd /tmp/build && cmake \
    -DCHAIN_SRC_ROOT=/work/bitcoin \
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
