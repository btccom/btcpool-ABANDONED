#
# Dockerfile
#
# @author yihao.peng@bitmain.com
# @copyright btc.com
# @since 2016-08-01
#
#
FROM unilynx/phusion-baseimage-1804:1.0.3
MAINTAINER YihaoPeng <yihao.peng@bitmain.com>

ENV HOME /root
ENV TERM xterm
CMD ["/sbin/my_init"]

# use aliyun source
ADD sources-aliyun.com.list /etc/apt/sources.list

RUN apt-get update
RUN apt-get install -y build-essential libtool cmake autotools-dev automake pkg-config libssl-dev libevent-dev bsdmainutils python3 git
RUN apt-get install -y libboost-all-dev libzmq3-dev curl wget net-tools
RUN apt-get install -y liblog4cpp5-dev libminiupnpc-dev libleveldb-dev libsnappy-dev libssl-dev libevent-dev libdb++-dev libgmp-dev

RUN mkdir ~/source

# build libsecp256k1
RUN cd ~/source && git clone --progress --depth=1 https://github.com/superbitcoin/secp256k1.git
RUN cd ~/source/secp256k1 \
  && ./autogen.sh \
  && ./configure --enable-module-recovery --enable-module-ecdh --enable-experimental \
  && make -j$(nproc) \
  && make install

# build bitcoind
RUN cd ~/source && wget -O v0.17.1.tar.gz https://github.com/YihaoPeng/SuperBitcoin/archive/v0.17.1p1.tar.gz
RUN cd ~/source \
  && tar zxf v0.17.1.tar.gz && cd SuperBitcoin-0.17.1* \
  && mkdir build && cd build \
  && cmake -DCMAKE_C_FLAGS="-Wno-unknown-pragmas -fmax-errors=1" -DENABLE_ZMQ_FLAG=ON .. \
  && make -j$(nproc) \
  && make install

# make some alias
RUN  ln -s sbtcd   /usr/local/bin/bitcoind \
  && ln -s sbtc-tx /usr/local/bin/bitcoin-tx \
  && ln -s /root/.bitcoin /root/.sbtc
ADD bitcoin-cli /usr/local/bin/
RUN cd /usr/local/bin && chmod +x bitcoin-cli

# mkdir bitcoind data dir
RUN mkdir -p /root/.bitcoin \
  && mkdir -p /root/default-conf

# default conf file
ADD log.conf /root/default-conf/log.conf

# logrotate
ADD logrotate-bitcoind /etc/logrotate.d/bitcoind

#
# services
#
# service for mainnet
RUN mkdir    /etc/service/bitcoind
ADD run      /etc/service/bitcoind/run
RUN chmod +x /etc/service/bitcoind/run
# service for testnet3
#RUN mkdir        /etc/service/bitcoind_testnet3
#ADD run_testnet3 /etc/service/bitcoind_testnet3/run
#RUN chmod +x     /etc/service/bitcoind_testnet3/run


# Cleaning work is useless, it only adds a new layer to the image, and increases its size.
# In addition, these files are useful if we want to modify the bitcoind source code 
# for debugging without rebuilding the docker.

# remove source & build files
#RUN rm -rf ~/source

# clean
#RUN apt-get clean && rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*

