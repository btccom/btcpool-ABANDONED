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
# error log
LOG=/var/log/opsgenie-monitor-namecoind.log


NAMECOIND_RPC="namecoin-cli "
#WANIP=`ip addr | grep 'state UP' -A2 | tail -n1 | awk '{print $2}' | cut -f1  -d'/'`
#WANIP=`curl https://api.ipify.org`
#WANIP=`curl http://ipinfo.io/ip`

RPCINFO=`$NAMECOIND_RPC getinfo`
#NOERROR=`echo "$RPCINFO" |  grep '"errors" : ""' | wc -l`
HEIGHT=`echo "$RPCINFO" | grep "blocks" | awk '{print $2}' | awk -F"," '{print $1}'`
CONNS=`echo "$RPCINFO" | grep "connections" | awk '{print $2}' | awk -F"," '{print $1}'`
DATE=`date "+%Y-%m-%d %H:%M:%S"`

#VALUE="height:$HEIGHT;conn:$CONNS;"

DATA=`cat << EOF
{"apiKey":"$API_KEY","name":"$SERVICE"}
EOF`

if [[ $CONNS -ne 0 ]]; then
  result=`curl -XPOST --max-time 30 -s -S $MURL -d "$DATA" 2>&1`
  success=`echo "$result" | grep successful`

  if [ "x$success" = "x" ]; then
    echo "[$DATE] api response is not successful: $result" >>$LOG
  fi

  echo "$result"
else
  echo "[$DATE] namecoind's connections is 0: $RPCINFO" >>$LOG
  echo "$RPCINFO"
fi
