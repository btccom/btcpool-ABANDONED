FROM phusion/baseimage:0.11
LABEL maintainer="Hanjiang Yu <hanjiang.yu@bitmain.com>"

ARG APT_MIRROR_URL

CMD ["/sbin/my_init"]

COPY update_apt_sources.sh /usr/local/bin/
RUN /usr/local/bin/update_apt_sources.sh "$APT_MIRROR_URL"

RUN install_clean \
  autoconf \
  automake \
  bsdmainutils \
  build-essential \
  libboost-all-dev \
  libevent-dev \
  libssl-dev \
  libtool \
  libzmq3-dev \
  pkg-config \
  wget \
  yasm

RUN cd /tmp && wget https://github.com/bitcoin-sv/bitcoin-sv/archive/v1.0.1.tar.gz && \
  [ $(sha256sum v1.0.1.tar.gz | cut -d " " -f 1) = "c803aa57f8c3a08294bedb3f7190f64660b8c9641c0c0b8ad9886e7fd8443b5f" ] && \
  tar zxf v1.0.1.tar.gz && cd bitcoin-sv-1.0.1 && ./autogen.sh && ./configure --disable-wallet --disable-tests --disable-bench && \
  make -j$(nproc) && make install && rm -r /tmp/*

# mkdir bitcoind data dir
RUN mkdir -p /root/.bitcoin

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
