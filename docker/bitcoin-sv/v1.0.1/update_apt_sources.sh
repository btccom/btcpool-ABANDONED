#!/bin/sh
set -e

. /etc/lsb-release

if [ -n "$1" ]; then
    echo "deb $1 ${DISTRIB_CODENAME} main restricted universe multiverse" > /etc/apt/sources.list
    for x in updates backports security; do
        echo "deb $1 ${DISTRIB_CODENAME}-${x} main restricted universe multiverse" >> /etc/apt/sources.list
    done
fi