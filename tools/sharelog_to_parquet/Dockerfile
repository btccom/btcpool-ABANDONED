#
# Dockerfile
#
# @author hanjiang.yu@bitmain.com
# @copyright btc.com
# @since 2018-12-01
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
    libprotobuf-dev \
    protobuf-compiler \
    pkg-config \
    wget \
    zlib1g-dev \
    apt-transport-https \
    lsb-release \
    gnupg \
 && LSB_RELEASE=`lsb_release --id --short | tr 'A-Z' 'a-z'` \
 && LSB_RELEASE_SHORT=`lsb_release --codename --short` \
 && wget -O- https://packages.red-data-tools.org/$LSB_RELEASE/red-data-tools-keyring.gpg | apt-key add - \
 && echo "deb https://packages.red-data-tools.org/$LSB_RELEASE/ $LSB_RELEASE_SHORT universe" > /etc/apt/sources.list.d/red-data-tools.list \
 && apt-get update \
 && apt install -y libarrow-dev libarrow-glib-dev libparquet-dev libparquet-glib-dev \
 && apt-get autoremove -y && apt-get clean q&& rm -rf /var/lib/apt/lists/*

COPY . /work/btcpool

RUN mkdir /work/build && cd /work/build && \
    cmake -DPOOL__GENERATE_DEB_PACKAGE=ON /work/btcpool/tools/sharelog_to_parquet && \
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
COPY tools/sharelog_to_parquet/entrypoint.sh /

# Install utilities & btcpool w/ debug symbols
RUN apt-get update  \
 && apt-get install -y php-cli wget apt-transport-https lsb-release gnupg \
 && LSB_RELEASE=`lsb_release --id --short | tr 'A-Z' 'a-z'` \
 && LSB_RELEASE_SHORT=`lsb_release --codename --short` \
 && wget -O- https://packages.red-data-tools.org/$LSB_RELEASE/red-data-tools-keyring.gpg | apt-key add - \
 && echo "deb https://packages.red-data-tools.org/$LSB_RELEASE/ $LSB_RELEASE_SHORT universe" > /etc/apt/sources.list.d/red-data-tools.list \
 && apt-get update \
 && apt-get install -y /work/package/*.deb \
 && apt-get autoremove -y \
 && apt-get clean \
 && rm -rf /var/lib/apt/lists/*

ENTRYPOINT ["/entrypoint.sh"]
