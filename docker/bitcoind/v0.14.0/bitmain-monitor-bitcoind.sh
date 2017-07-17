#! /bin/bash
#
# bitcoind monitor shell
#
# @copyright btc.com
# @author Zhibiao Pan
# @since 2016-08
#
SROOT=$(cd $(dirname "$0"); pwd)
cd $SROOT

BITCOIND_RPC="bitcoin-cli "
#WANIP=`ip addr | grep 'state UP' -A2 | tail -n1 | awk '{print $2}' | cut -f1  -d'/'`
#WANIP=`curl https://api.ipify.org`
#WANIP=`curl http://ipinfo.io/ip`
WANIP=`curl -sL https://ip.btc.com`

#NOERROR=`$BITCOIND_RPC getinfo |  grep '"errors" : ""' | wc -l`
HEIGHT=`$BITCOIND_RPC getinfo | grep "blocks" | awk '{print $2}' | awk -F"," '{print $1}'`
CONNS=`$BITCOIND_RPC getinfo | grep "connections" | awk '{print $2}' | awk -F"," '{print $1}'`

SERVICE="bpool.bitcoind.$WANIP"
VALUE="height:$HEIGHT;conn:$CONNS;"
MURL="https://monitor.btc.com/monitor/api/v1/message?service=$SERVICE&value=$VALUE"

if [[ $CONNS -ne 0 ]]; then
  curl --max-time 30 $MURL
  exit 0
fi
