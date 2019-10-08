#!/bin/sh
# run proxy
set -e

cd "$( dirname "$0" )"

tryMkdir() {
    if ! [ -e "$1" ]; then
        mkdir -p "$1"
        usr="$2"
        if [ "x$usr" = "x" ]; then
            usr="999"
        fi
        chown -R "$usr:$usr" "$1"
    fi
}

tryCopy() {
    if ! [ -e "$2" ]; then
        cp "$1" "$2"
        usr="$3"
        if [ "x$usr" = "x" ]; then
            usr="999"
        fi
        chown -R "$usr:$usr" "$2"
    fi
}

tryMkdir ./data/block-update
tryMkdir ./data/block-update/log
tryMkdir ./data/block-update/sql

tryMkdir ./data/eth-node
tryMkdir ./data/eth-node/data
tryMkdir ./data/eth-node/log

tryMkdir ./data/grafana 472
tryMkdir ./data/grafana/data 472
tryMkdir ./data/grafana/log 472

tryMkdir ./data/mysql
tryMkdir ./data/mysql/data
tryMkdir ./data/mysql/sql
tryMkdir ./data/mysql/log

tryMkdir ./data/proxy
tryMkdir ./data/proxy/cfg
tryMkdir ./data/proxy/log

tryMkdir ./data/phpmyadmin
tryMkdir ./data/phpmyadmin/log

tryCopy ./stratum_log_proxy.cfg ./data/proxy/cfg/stratum_log_proxy.cfg
tryCopy ./sql/create-table.sql ./data/mysql/sql/create-table.sql
tryCopy ./sql/update-share-info.sql ./data/block-update/sql/update-share-info.sql

docker-compose up "$@"
