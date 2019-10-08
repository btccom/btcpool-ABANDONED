#!/bin/sh
# import docker images
set -e

cd "$( dirname "$0" )/img"
docker load -i stratum-log-proxy.img.gz
docker load -i get-eth-header.img.gz
docker load -i geth.img.gz
docker load -i mariadb.img.gz
docker load -i grafana.img.gz
docker load -i phpmyadmin.img.gz
