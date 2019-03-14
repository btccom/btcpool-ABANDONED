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

RUN apt-get update
RUN apt-get install -y build-essential libtool pkg-config libssl-dev
RUN apt-get install -y libboost-all-dev curl wget unzip git cmake

# build bitcoind
RUN mkdir /work
RUN cd /work && git clone -b v2.0.4431-btcpool --depth 1 https://github.com/btccom/beam.git
RUN cd /work/beam \
  && cmake -DCMAKE_BUILD_TYPE=Release -DBEAM_NO_QT_UI_WALLET=ON . \
  #&& make && make install
  && make -j$(nproc)

# mkdir bitcoind data dir
RUN mkdir -p /work/beam-node

#
# services
#

# service for mainnet
RUN mkdir    /etc/service/beam-node
ADD run      /etc/service/beam-node/run
RUN chmod +x /etc/service/beam-node/run
