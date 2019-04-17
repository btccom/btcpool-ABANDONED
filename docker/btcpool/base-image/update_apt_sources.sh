#!/bin/sh
set -e

if [ -n "${APT_MIRROR_URL}" ]; then
    echo "deb ${APT_MIRROR_URL} $(lsb_release -cs) main restricted universe multiverse" > /etc/apt/sources.list
    for x in updates backports security; do
        echo "deb ${APT_MIRROR_URL} $(lsb_release -cs)-${x} main restricted universe multiverse" >> /etc/apt/sources.list
    done
fi