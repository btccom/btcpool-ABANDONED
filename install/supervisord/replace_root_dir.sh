#!/bin/bash

#default root dir is /work/btcpool
default_dir="/work/btcpool"
new_root_dir=$1

if [ -d "$new_root_dir" ]; then
    cd $new_root_dir
    new_root_dir=`pwd`
    echo "replacing /work/btcpool with $new_root_dir"

    escaped_default_dir=${default_dir//\//\\/}
    escaped_new_root_dir=${new_root_dir//\//\\/}

    syntax="s/${escaped_default_dir}/${escaped_new_root_dir}/g"
    sed -i -e ${syntax} blkmaker.conf
    sed -i -e ${syntax} gbtmaker.conf
    sed -i -e ${syntax} gwmaker.conf
    sed -i -e ${syntax} jobmaker.conf
    sed -i -e ${syntax} poolwatcher.conf
    sed -i -e ${syntax} sserver.conf

else
    echo "$new_root_dir not exist"
fi