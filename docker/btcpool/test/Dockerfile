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
LABEL maintainer="YihaoPeng Yu <yihao.peng@bitmain.com>"

ARG GIT_DESCRIBE=""
ARG BUILD_JOBS=1
ARG BUILD_TESTING=ON
ARG BUILD_TYPE=Release
ARG WORK_WITH_STRATUM_SWITCHER=OFF

# Copy & build btcpool
COPY . /work/btcpool
RUN mkdir -p /work/btcpool/build && cd /work/btcpool/build && cmake \
    -DBPOOL_GIT_DESCRIBE=${GIT_DESCRIBE} \
    -DBUILD_TESTING=${BUILD_TESTING} \
    -DCHAIN_SRC_ROOT=/work/blockchain \
    -DCHAIN_TYPE=${CHAIN_TYPE} \
    -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
    -DJOBS=${BUILD_JOBS} \
    -DKAFKA_LINK_STATIC=ON \
    -DPOOL__GENERATE_DEB_PACKAGE=ON \
    -DPOOL__INSTALL_PREFIX=/work/btcpool \
    -DPOOL__WORK_WITH_STRATUM_SWITCHER=${WORK_WITH_STRATUM_SWITCHER} \
    /work/btcpool
RUN cd /work/btcpool/build && make -j${BUILD_JOBS}

# startup scripts
COPY ./docker/btcpool/deploy/entrypoint.sh ./docker/btcpool/deploy/wait-for-it.sh /

# entrypoint
ENTRYPOINT ["/entrypoint.sh"]
