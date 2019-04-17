#!/bin/sh
set -e

if [ -n "${APT_MIRROR_URL}" ]; then
    echo "deb ${APT_MIRROR_URL} bionic main restricted universe multiverse" > /etc/apt/sources.list
    for x in updates backports security; do
        echo "deb ${APT_MIRROR_URL} bionic-${x} main restricted universe multiverse" >> /etc/apt/sources.list
    done
fi