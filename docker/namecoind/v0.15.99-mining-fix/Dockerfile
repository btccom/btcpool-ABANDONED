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
ADD sources-aliyun.com.list /etc/apt/sources.list

RUN apt-get update
RUN apt-get install -y build-essential libtool autotools-dev automake pkg-config libssl-dev libevent-dev bsdmainutils yasm
RUN apt-get install -y libboost-all-dev libzmq3-dev curl wget

# download namecoind
RUN mkdir ~/source
RUN cd ~/source && wget https://github.com/btccom/namecoin-core/archive/nc0.15.99-mining-fix.tar.gz -O nc0.15.99-mining-fix.tar.gz \
  && tar zxf nc0.15.99-mining-fix.tar.gz

# bdb 4.8
RUN cd ~/source/namecoin-core-nc0.15.99-mining-fix \
  && mkdir -p /usr/local/db4 \
  && wget 'http://download.oracle.com/berkeley-db/db-4.8.30.NC.tar.gz' \
  && tar -xzvf db-4.8.30.NC.tar.gz \
  && cd db-4.8.30.NC/build_unix \
  && ../dist/configure --enable-cxx --disable-shared --with-pic --prefix=/usr/local/db4 \
  && make install

# build namecoind
RUN cd ~/source/namecoin-core-nc0.15.99-mining-fix \
  && ./autogen.sh \
  && ./configure LDFLAGS="-L/usr/local/db4/lib" CPPFLAGS="-I/usr/local/db4/include" --without-miniupnpc \
  && make -j && make install

# mkdir namecoind data dir
RUN mkdir -p /root/.namecoin
RUN mkdir -p /root/scripts

# logrotate
ADD logrotate-namecoind /etc/logrotate.d/namecoind

# services
RUN mkdir /etc/service/namecoind
ADD run /etc/service/namecoind/run
RUN chmod +x /etc/service/namecoind/run

# remove source & build files (cannot reduce the image size)
#RUN rm -rf ~/source

# clean (cannot reduce the image size)
#RUN apt-get clean && rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*
