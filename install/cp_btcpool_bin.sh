#!/bin/bash

if [ $# -le 1 ]
  then
    echo "error arguments"
    echo "arguments: [bin source dir] [bin dest dir]"
    exit 1
fi

bin_source=$(cd $1; pwd)
bin_dest=$(cd "$(dirname "$2")"; pwd)

cp ${bin_source}/blkmaker $bin_dest
cp ${bin_source}/gbtmaker $bin_dest
cp ${bin_source}/gwmaker $bin_dest
cp ${bin_source}/jobmaker $bin_dest
cp ${bin_source}/nmcauxmaker $bin_dest
cp ${bin_source}/poolwatcher $bin_dest
cp ${bin_source}/sharelogger $bin_dest
cp ${bin_source}/slparser $bin_dest
cp ${bin_source}/sserver $bin_dest
cp ${bin_source}/statshttpd $bin_dest
