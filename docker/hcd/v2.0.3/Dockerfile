#
# Dockerfile
#
# @author hanjiang.yu@bitmain.com
# @copyright btc.com
# @since 2018-09-10
#
#
FROM phusion/baseimage:0.11
LABEL maintainer="Hanjiang Yu <hanjiang.yu@bitmain.com>"
#
#ENV HOME /root
#ENV TERM xterm
#CMD ["/sbin/my_init"]

# use aliyun source
#ADD sources-aliyun.com.list /etc/apt/sources.list

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    unzip \
    wget \
    && apt-get autoremove && apt-get clean && rm -rf /var/lib/apt/lists/*

# download hcd
RUN cd /tmp && wget https://github.com/HcashOrg/hcd/files/2317443/linux_amd64_hcd.zip && \
    [ $(sha256sum linux_amd64_hcd.zip | cut -d " " -f 1) = "5c5dafaab435f022d1233b2da897c281b0605c50195dbd97ab612adbcff6e731" ] && \
    unzip linux_amd64_hcd.zip && chmod +x linux_amd64_hcd && cp linux_amd64_hcd /usr/local/bin/ && rm -rf /tmp/*

# mkdir hcd data dir
RUN mkdir -p /root/.hcd

# logrotate
ADD logrotate-hcd /etc/logrotate.d/hcd

# service for hcd
RUN mkdir    /etc/service/hcd
ADD run      /etc/service/hcd/run
RUN chmod +x /etc/service/hcd/run
