#
# Dockerfile
#
# @author zhibiao.pan@bitmain.com, yihao.peng@bitmain.com
# @copyright btc.com
# @since 2016-08-01
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
RUN apt-get install -y build-essential libtool autotools-dev automake pkg-config libssl-dev libevent-dev bsdmainutils yasm
RUN apt-get install -y libboost-all-dev libzmq3-dev curl wget

# build bitcoind
RUN mkdir ~/source
RUN cd ~/source && wget https://github.com/Bitcoin-ABC/bitcoin-abc/archive/v0.16.1.tar.gz
RUN cd ~/source \
  && tar zxf v0.16.1.tar.gz && cd bitcoin-abc-0.16.1 \
  && ./autogen.sh \
  && ./configure --disable-wallet --disable-tests \
  && make && make install
  #&& make -j$(nproc) && make install

# mkdir bitcoind data dir
RUN mkdir -p /root/.bitcoin
RUN mkdir -p /root/scripts

# scripts
ADD opsgenie-monitor-bitcoind.sh   /root/scripts/opsgenie-monitor-bitcoind.sh

# crontab shell
ADD crontab.txt /etc/cron.d/bitcoind

# logrotate
ADD logrotate-bitcoind /etc/logrotate.d/bitcoind

#
# services
#

# service for mainnet
RUN mkdir    /etc/service/bitcoind
ADD run      /etc/service/bitcoind/run
RUN chmod +x /etc/service/bitcoind/run

#service for testnet
#RUN mkdir        /etc/service/bitcoind_testnet
#ADD run_testnet /etc/service/bitcoind_testnet/run
#RUN chmod +x     /etc/service/bitcoind_testnet/run

# remove source & build files
RUN rm -rf ~/source

# clean
RUN apt-get clean && rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*
