#
# Dockerfile
#
# @author hanjiang.yu@bitmain.com
# @copyright btc.com
# @since 2018-12-01
#
#
FROM ubuntu:18.04
LABEL maintainer="Hanjiang Yu <hanjiang.yu@bitmain.com>"

ARG APT_MIRROR_URL

COPY update_apt_sources.sh /tmp
RUN /tmp/update_apt_sources.sh

RUN apt-get update && apt-get install -y \
    clang-format \
    git \
    && apt-get autoremove && apt-get clean q&& rm -rf /var/lib/apt/lists/*
