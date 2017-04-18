#! /bin/bash
#
# namecoind monitor shell
#
# @copyright btc.com
# @author Zhibiao Pan, Yihao Peng
# @since 2016-09
#
SROOT=$(cd $(dirname "$0"); pwd)
cd $SROOT


# the name of https://app.opsgenie.com/heartbeat
SERVICE="namecoind.01"

# api key of opsgenie
API_KEY="xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"

# api endpoint
MURL="https://api.opsgenie.com/v1/json/heartbeat/send"


NAMECOIND_RPC="namecoin-cli "
#WANIP=`ip addr | grep 'state UP' -A2 | tail -n1 | awk '{print $2}' | cut -f1  -d'/'`
#WANIP=`curl https://api.ipify.org`
#WANIP=`curl http://ipinfo.io/ip`

#NOERROR=`$NAMECOIND_RPC getinfo |  grep '"errors" : ""' | wc -l`
HEIGHT=`$NAMECOIND_RPC getinfo | grep "blocks" | awk '{print $2}' | awk -F"," '{print $1}'`
CONNS=`$NAMECOIND_RPC getinfo | grep "connections" | awk '{print $2}' | awk -F"," '{print $1}'`

#VALUE="height:$HEIGHT;conn:$CONNS;"

DATA=`cat << EOF
{"apiKey":"$API_KEY","name":"$SERVICE"}
EOF`

if [[ $CONNS -ne 0 ]]; then
  curl -XPOST --max-time 30 $MURL -d "$DATA"
  exit 0
fi
