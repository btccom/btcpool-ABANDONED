#
# Dockerfile
#
# @author yihao.peng@bitmain.com
# @copyright btc.com
# @since 2019-10-09
#
#
FROM golang as build
LABEL maintainer="Yihao Peng <yihao.peng@bitmain.com>"

COPY get_eth_header.go /work/get_eth_header/get_eth_header.go
RUN go get github.com/go-sql-driver/mysql && \
    go get github.com/golang/glog && \
    go get github.com/gorilla/websocket && \
    cd /work/get_eth_header && go build

######### Release image #########
FROM ubuntu:18.04
LABEL maintainer="Yihao Peng <yihao.peng@bitmain.com>"

ARG APT_MIRROR_URL

COPY --from=build /work/get_eth_header/get_eth_header /usr/local/bin/get_eth_header

COPY update_apt_sources.sh wait-for-it.sh /tmp/

ENTRYPOINT ["get_eth_header"]
