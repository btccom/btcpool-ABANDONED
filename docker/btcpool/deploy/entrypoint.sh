#!/bin/bash
set -e
export PATH=$PATH:/work/btcpool/build

# fix DNS issue
# "Random case of domain name causes asynchronous DNS resolution of libevent2 to fail"
# https://github.com/pgbouncer/pgbouncer/issues/122
# https://www.cloudxns.net/Support/detail/id/933.html
if ! grep 'options randomize-case:0' /etc/resolv.conf &>/dev/null; then
    echo "options randomize-case:0" >> /etc/resolv.conf
fi

exec "$@"
