#!/bin/sh
# build/pull/export docker images
set -e

echo "build stratum-log-proxy"
cd "$( dirname "$0" )"
docker build -t stratum-log-proxy --build-arg APT_MIRROR_URL="$APT_MIRROR_URL" -f Dockerfile ../..  

echo "build get-eth-header"
cd get_eth_header
docker build -t get-eth-header .

echo "pull geth"
docker pull ethereum/client-go:stable

echo "pull mariadb"
docker pull mariadb

echo "pull grafana"
docker pull grafana/grafana

echo "pull phpmyadmin"
docker pull phpmyadmin/phpmyadmin

echo 'export docker images to ./img/'
[ -e "../img" ] || mkdir ../img
cd ../img
docker save stratum-log-proxy | gzip > stratum-log-proxy.img.gz
docker save get-eth-header | gzip > get-eth-header.img.gz
docker save ethereum/client-go:stable | gzip > geth.img.gz
docker save mariadb | gzip > mariadb.img.gz
docker save grafana/grafana | gzip > grafana.img.gz
docker save phpmyadmin/phpmyadmin | gzip > phpmyadmin.img.gz
