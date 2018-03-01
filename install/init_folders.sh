#!/bin/bash
#
# init run folders for btcpool
#
# @copyright btc.com
# @author Kevin Pan
# @since 2016-08
#

# No longer restrict the installation folder
# cd /work/btcpool/build

sourceDir=$(cd "$(dirname "$0")"; pwd)"/../src"

# blkmaker
if [ ! -d "run_blkmaker" ]; then
  mkdir "run_blkmaker" && cd "run_blkmaker"
  mkdir "log_blkmaker"
  ln -s ../blkmaker .
  cp $sourceDir/blkmaker/blkmaker.cfg .
  cd ..
fi

# gbtmaker
if [ ! -d "run_gbtmaker" ]; then
  mkdir "run_gbtmaker" && cd "run_gbtmaker"
  mkdir "log_gbtmaker"
  ln -s ../gbtmaker .
  cp $sourceDir/gbtmaker/gbtmaker.cfg .
  cd ..
fi

# gwmaker
if [ ! -d "run_gwmaker" ]; then
  mkdir "run_gwmaker" && cd "run_gwmaker"
  mkdir "log_gwmaker"
  ln -s ../gwmaker .
  cp $sourceDir/gwmaker/gwmaker.cfg .
  cd ..
fi

# jobmaker
if [ ! -d "run_jobmaker" ]; then
  mkdir "run_jobmaker" && cd "run_jobmaker"
  mkdir "log_jobmaker"
  ln -s ../jobmaker .
  cp $sourceDir/jobmaker/jobmaker.cfg .
  cd ..
fi

# sharelogger
if [ ! -d "run_sharelogger" ]; then
  mkdir "run_sharelogger" && cd "run_sharelogger"
  mkdir "log_sharelogger"
  ln -s ../sharelogger .
  cp $sourceDir/sharelogger/sharelogger.cfg .
  cd ..
fi

# slparser
if [ ! -d "run_slparser" ]; then
  mkdir "run_slparser" && cd "run_slparser"
  mkdir "log_slparser"
  ln -s ../slparser .
  cp $sourceDir/slparser/slparser.cfg .
  cd ..
fi

# sserver
if [ ! -d "run_sserver" ]; then
  mkdir "run_sserver" && cd "run_sserver"
  mkdir "log_sserver"
  ln -s ../sserver .
  cp $sourceDir/sserver/sserver.cfg .
  cd ..
fi

# statshttpd
if [ ! -d "run_statshttpd" ]; then
  mkdir "run_statshttpd" && cd "run_statshttpd"
  mkdir "log_statshttpd"
  ln -s ../statshttpd .
  cp $sourceDir/statshttpd/statshttpd.cfg .
  cd ..
fi

# poolwatcher
if [ ! -d "run_poolwatcher" ]; then
  mkdir "run_poolwatcher" && cd "run_poolwatcher"
  mkdir "log_poolwatcher"
  ln -s ../poolwatcher .
  cp $sourceDir/poolwatcher/poolwatcher.cfg .
  cd ..
fi

# simulator
if [ ! -d "run_simulator" ]; then
  mkdir "run_simulator" && cd "run_simulator"
  mkdir "log_simulator"
  ln -s ../simulator .
  cp $sourceDir/simulator/simulator.cfg .
  cd ..
fi

# nmcauxmaker
if [ ! -d "run_nmcauxmaker" ]; then
  mkdir "run_nmcauxmaker" && cd "run_nmcauxmaker"
  mkdir "log_nmcauxmaker"
  ln -s ../nmcauxmaker .
  cp $sourceDir/nmcauxmaker/nmcauxmaker.cfg .
  cd ..
fi
