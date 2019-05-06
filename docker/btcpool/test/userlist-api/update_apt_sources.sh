#!/bin/sh
set -e

. /etc/lsb-release

if [ -n "${APT_MIRROR_URL}" ]; then
    echo "deb ${APT_MIRROR_URL} ${DISTRIB_CODENAME} main restricted universe multiverse" > /etc/apt/sources.list
    for x in updates backports security; do
        echo "deb ${APT_MIRROR_URL} ${DISTRIB_CODENAME}-${x} main restricted universe multiverse" >> /etc/apt/sources.list
    done
fi