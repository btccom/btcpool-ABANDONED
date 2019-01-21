#
# Dockerfile
#
# @author hanjiang.yu@bitmain.com
# @copyright btc.com
# @since 2018-09-10
#
#
FROM phusion/baseimage:0.10.2
LABEL maintainer="Hanjiang Yu <hanjiang.yu@bitmain.com>"

ENV HOME /root
ENV TERM xterm
CMD ["/sbin/my_init"]

# use aliyun source
#ADD sources-aliyun.com.list /etc/apt/sources.list

RUN apt-get update && apt-get install -y net-tools wget git build-essential

# download golang
RUN mkdir ~/source
RUN cd ~/source && wget https://dl.google.com/go/go1.11.4.linux-amd64.tar.gz && \
    cd /usr/local && \
    tar zxf ~/source/go1.11.4.linux-amd64.tar.gz && \
    ln -s /usr/local/go/bin/go /usr/local/bin/go

# clone dcrd source code
RUN cd ~/source && git clone --depth=1 -b v1.4.0-rc1-debug https://github.com/btccom/dcrd.git
RUN cd ~/source/dcrd && \
    go install . ./cmd/... && \
    ln -s ~/go/bin/dcrd ~/go/bin/dcrctl /usr/local/bin/

# mkdir dcrd data dir
RUN mkdir -p /root/.dcrd

#
# services
#

# service for dcrd
RUN mkdir    /etc/service/dcrd
ADD run      /etc/service/dcrd/run
RUN chmod +x /etc/service/dcrd/run
