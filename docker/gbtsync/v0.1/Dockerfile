#
# Dockerfile
#
# @author yihao.peng@bitmain.com
# @copyright btc.com
# @since 2018-05-17
#
#
FROM phusion/baseimage:0.9.22
MAINTAINER YihaoPeng <yihao.peng@bitmain.com>

ENV HOME /root
ENV TERM xterm
CMD ["/sbin/my_init"]

# use aliyun source
#ADD sources-aliyun.com.list /etc/apt/sources.list

RUN apt-get update
RUN apt-get install -y build-essential cmake libgoogle-glog-dev libmysqlclient-dev curl wget unzip

# build bitcoind
RUN mkdir ~/source
RUN cd ~/source && wget -O btcpool-lightgbt.zip https://github.com/btccom/btcpool/archive/lightgbt.zip
RUN cd ~/source \
  && unzip -q btcpool-lightgbt.zip \
  && cd btcpool-lightgbt \
  && mkdir build \
  && cd build \
  && cmake ../src/gbtsync \
  && make -j$(nproc) \
  && make install

# mkdir bitcoind data dir
RUN mkdir -p /datadir

# service
RUN mkdir    /etc/service/gbtsync
ADD run      /etc/service/gbtsync/run
RUN chmod +x /etc/service/gbtsync/run
