#
# Dockerfile
#
# @author yihao.peng@bitmain.com
# @copyright btc.com
# @since 2016-08-01
#
#

######### build image #########
FROM ubuntu:18.04 as build
MAINTAINER YihaoPeng <yihao.peng@bitmain.com>

# Number of concurrent processes at build time
ARG JOBS

ENV HOME /root
ENV TERM xterm

# use aliyun source
ADD sources-aliyun.com.list /etc/apt/sources.list

# add PPA and update software sources
#RUN apt-get install software-properties-common
#RUN add-apt-repository ppa:bitcoin/bitcoin
RUN apt-get update

# install dependencies for btcpool and btc/bch
RUN apt-get install -y git build-essential libtool autotools-dev autoconf automake \
        pkg-config libssl-dev bsdmainutils yasm openssl libcurl4-openssl-dev \
        libconfig++-dev libboost-all-dev libgmp-dev libmysqlclient-dev libzookeeper-mt-dev \
        libzmq3-dev libgoogle-glog-dev cmake wget zlib1g zlib1g-dev libhiredis-dev && \
        #libdb4.8-dev libdb4.8++-dev && \
    apt-get autoremove

# create dirs
RUN mkdir -p /work

# build libevent static library
#
# Notice: the release of libevent has a dead lock bug,
#         so use the code for the master branch here.
# Issue:  sserver accidental deadlock when release StratumSession
#         from consume thread
#         <https://github.com/btccom/btcpool/issues/75>
RUN cd /work && \
    git clone https://github.com/libevent/libevent.git && \
    cd libevent && \
    ./autogen.sh && \
    ./configure --disable-shared && \
    make -j$JOBS && \
    make install

# build kafka static library
RUN cd /work && \
    wget -O librdkafka-0.9.1.tar.gz https://github.com/edenhill/librdkafka/archive/0.9.1.tar.gz && \
    tar zxf librdkafka-0.9.1.tar.gz && \
    cd librdkafka-0.9.1 && \
    ./configure && \
    make -j$JOBS && \
    make install

# remove dynamic libraries of librdkafka
# In this way, the constructed deb package will
# not have dependencies that not from software sources.
RUN cd /usr/local/lib && \
    find . | grep 'rdkafka' | grep '.so' | xargs rm

# download bitcoin-core
RUN cd /work && \
    wget -O bitcoin-abc-0.17.1.tar.gz https://github.com/Bitcoin-ABC/bitcoin-abc/archive/v0.17.1.tar.gz && \
    tar zxf bitcoin-abc-0.17.1.tar.gz

# clone btcpool
RUN cd /work && \
    git clone -b master https://github.com/btccom/btcpool.git && \
    mkdir /work/btcpool/build

# cmake
RUN cd /work/btcpool/build && \
    cmake -DJOBS=$JOBS \
          -DCMAKE_BUILD_TYPE=Release \
          -DCHAIN_TYPE=BCH \
          -DCHAIN_SRC_ROOT=/work/bitcoin-abc-0.17.1 \
          -DPOOL__GENERATE_DEB_PACKAGE=ON \
          -DPOOL__INSTALL_PREFIX=/work/btcpool \
          -DPOOL__WORK_WITH_STRATUM_SWITCHER=ON ..

# make
RUN cd /work/btcpool/build && \
    make -j$JOBS

# make package
RUN cd /work/btcpool/build && \
    make package -j && \
    mkdir /work/package && \
    cp *.deb /work/package

######### release image #########
FROM ubuntu:18.04
MAINTAINER YihaoPeng <yihao.peng@bitmain.com>

ENV HOME /root
ENV TERM xterm

# copy deb packages
COPY --from=build /work/package /work/package

# use aliyun source
ADD sources-aliyun.com.list /etc/apt/sources.list

# install deb packages
RUN apt-get update
RUN dpkg -i /work/package/*-main.deb || echo dependencies && \
    apt-get install -y -f

# add DNS
RUN echo nameserver 114.114.114.114 >> /etc/resolv.conf && \
    echo nameserver 223.5.5.5       >> /etc/resolv.conf && \
    echo nameserver 8.8.8.8         >> /etc/resolv.conf

# startup command
ENTRYPOINT ["/work/btcpool/build/sserver", "-c", "/work/btcpool/build/run_sserver/sserver.cfg", "-l", "/work/btcpool/build/run_sserver/log_sserver"]
