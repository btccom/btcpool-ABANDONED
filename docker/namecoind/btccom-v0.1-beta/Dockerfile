#
# Dockerfile
#
# @author zhibiao.pan@bitmain.com
# @copyright btc.com
# @since 2016-08-01
#
#
FROM phusion/baseimage:0.9.19
MAINTAINER PanZhibiao <zhibiao.pan@bitmain.com>

ENV HOME /root
ENV TERM xterm
CMD ["/sbin/my_init"]

# use aliyun source
#ADD sources-aliyun.com.list /etc/apt/sources.list

RUN apt-get update
RUN apt-get install -y build-essential libtool autotools-dev automake pkg-config libssl-dev libevent-dev bsdmainutils yasm
RUN apt-get install -y libboost-all-dev libzmq3-dev curl wget

# download namecoind
RUN mkdir ~/source
RUN cd ~/source && wget https://github.com/btccom/namecoin-core/archive/btccom-v0.1-beta.tar.gz -O btccom-v0.1-beta.tar.gz \
  && tar zxf btccom-v0.1-beta.tar.gz

# build namecoind
RUN cd ~/source/namecoin-core-btccom-v0.1-beta \
  && ./autogen.sh \
  && ./configure --disable-wallet --without-miniupnpc --disable-tests \
  && make && make install
  #&& make -j$(nproc) && make install

# mkdir namecoind data dir
RUN mkdir -p /root/.namecoin
RUN mkdir -p /root/scripts

# scripts
ADD opsgenie-monitor-namecoind.sh  /root/scripts/opsgenie-monitor-namecoind.sh
ADD watch-namecoind.sh             /root/scripts/watch-namecoind.sh

# crontab shell
ADD crontab.txt /etc/cron.d/namecoind

# logrotate
ADD logrotate-namecoind /etc/logrotate.d/namecoind

# services
RUN mkdir /etc/service/namecoind
ADD run /etc/service/namecoind/run
RUN chmod +x /etc/service/namecoind/run

# remove source & build files
RUN rm -rf ~/source

# clean
RUN apt-get clean && rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*
