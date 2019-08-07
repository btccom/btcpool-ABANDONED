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

ENV HOME /root
ENV TERM xterm
CMD ["/sbin/my_init"]

# use aliyun source
ADD sources-aliyun.com.list /etc/apt/sources.list

RUN apt-get update && apt-get install -y build-essential libtool pkg-config libssl-dev libboost-all-dev curl wget unzip git && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# download cmake
RUN cd /usr/local && \
    wget https://github.com/Kitware/CMake/releases/download/v3.15.1/cmake-3.15.1-Linux-x86_64.tar.gz && \
    tar zxf cmake-3.15.1-Linux-x86_64.tar.gz --strip-components=1 && \
    rm cmake-3.15.1-Linux-x86_64.tar.gz

# build beam-node
RUN mkdir /work && cd /work && git clone -b mining-hard-fork --depth 1 https://github.com/btccom/beam.git
RUN cd /work/beam \
  && cmake -DCMAKE_BUILD_TYPE=Release -DBEAM_NO_QT_UI_WALLET=ON . \
  #&& make && make install
  && make -j$(nproc)

# mkdir dirs
RUN mkdir -p /work/beam-node /etc/service/beam-node

ADD run      /etc/service/beam-node/run
RUN chmod +x /etc/service/beam-node/run
