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
RUN apt-get install -y wget

# build bitcoind
RUN mkdir -p /work/bin
RUN cd /work/bin \
  && wget https://builds.beam.mw/mainnet/2019.02.11/Release/linux/beam-node-1.2.4419.tar.gz \
  && tar zxf beam-node-1.2.4419.tar.gz

# mkdir bitcoind data dir
RUN mkdir -p /work/beam-node

#
# services
#

# service for mainnet
RUN mkdir    /etc/service/beam-node
ADD run      /etc/service/beam-node/run
RUN chmod +x /etc/service/beam-node/run
