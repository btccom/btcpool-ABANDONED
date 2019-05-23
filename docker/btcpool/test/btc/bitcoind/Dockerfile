#
# Dockerfile
#
# @author yihao.peng@bitmain.com
#
FROM phusion/baseimage:0.10.2
MAINTAINER YihaoPeng <yihao.peng@bitmain.com>

ENV HOME /root
ENV TERM xterm
CMD ["/sbin/my_init"]

# use aliyun source
ADD sources-aliyun.com.list /etc/apt/sources.list

RUN apt-get update
RUN apt-get install -y build-essential libtool autotools-dev automake pkg-config libssl-dev libevent-dev bsdmainutils python3
RUN apt-get install -y libboost-all-dev libzmq3-dev curl wget net-tools

# build bitcoind
RUN mkdir ~/source
RUN cd ~/source && wget https://github.com/bitcoin/bitcoin/archive/v0.16.3.tar.gz
RUN cd ~/source \
  && tar zxf v0.16.3.tar.gz && cd bitcoin-0.16.3 \
  && ./autogen.sh \
  && ./configure --disable-wallet --disable-tests \
  && make -j$(nproc) && make install

# mkdir bitcoind data dir
RUN mkdir -p /work/bitcoin1 /work/bitcoin2
RUN mkdir -p /root/scripts

# add config files
ADD bitcoin1.conf /work/bitcoin1/bitcoin.conf
ADD bitcoin2.conf /work/bitcoin2/bitcoin.conf

# logrotate
ADD logrotate-bitcoind /etc/logrotate.d/bitcoind

#
# services
#
# service for bitcoin1
RUN mkdir /etc/service/bitcoin1
ADD run-bitcoin1 /etc/service/bitcoin1/run
RUN chmod +x /etc/service/bitcoin1/run

# service for bitcoin1
RUN mkdir /etc/service/bitcoin2
ADD run-bitcoin2 /etc/service/bitcoin2/run
RUN chmod +x /etc/service/bitcoin2/run

# service for bitcoin-cli
#RUN mkdir /etc/service/bitcoin-cli
#ADD run-cli /etc/service/bitcoin-cli/run
#RUN chmod +x /etc/service/bitcoin-cli/run
