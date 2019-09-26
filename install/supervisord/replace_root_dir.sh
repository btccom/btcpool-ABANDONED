#!/bin/bash

# script to help replace btcpool supervisor's .conf file.
# init_package_folder allow us to put installation folder other than /work/btcpool/build, so this script is to help replacing the installation folder
 
cur_dir=`pwd`
echo $cur_dir
default_dir="/work/btcpool/build"
new_root_dir=$1

if [ -d "$new_root_dir" ]; then
    cd $new_root_dir
    new_root_dir=`pwd`
    echo "replacing /work/btcpool with $new_root_dir"

    escaped_default_dir=${default_dir//\//\\/}
    escaped_new_root_dir=${new_root_dir//\//\\/}

    syntax="s/${escaped_default_dir}/${escaped_new_root_dir}/g"
    sed -i -e ${syntax} ${cur_dir}/blkmaker.conf
    sed -i -e ${syntax} ${cur_dir}/gbtmaker.conf
    sed -i -e ${syntax} ${cur_dir}/gwmaker.conf
    sed -i -e ${syntax} ${cur_dir}/jobmaker.conf
    sed -i -e ${syntax} ${cur_dir}/nmcauxmaker.conf
    sed -i -e ${syntax} ${cur_dir}/poolwatcher.conf
    sed -i -e ${syntax} ${cur_dir}/sharelogger.conf
    sed -i -e ${syntax} ${cur_dir}/slparser.conf
    sed -i -e ${syntax} ${cur_dir}/sserver.conf
    sed -i -e ${syntax} ${cur_dir}/statshttpd.conf

else
    echo "$new_root_dir not exist"
fi