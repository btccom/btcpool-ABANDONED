#! /bin/bash
#
# bitcoind monitor shell
#
# @copyright btc.com
# @author Yihao Peng
# @since 2017-07
#
SROOT=$(cd $(dirname "$0"); pwd)
cd $SROOT

# the name of https://app.opsgenie.com/heartbeat
SERVICE="bcc-bitcoind.1"
# api key of opsgenie
API_KEY="xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
# api endpoint
MURL="https://api.opsgenie.com/v1/json/heartbeat/send"
# error log
LOG=/var/log/opsgenie-monitor-bitcoind.log

BITCOIND_RPC="bitcoin-cli "
#WANIP=`curl -sL https://ip.btc.com`

#NOERROR=`$BITCOIND_RPC getinfo |  grep '"errors" : ""' | wc -l`
HEIGHT=`$BITCOIND_RPC getinfo | grep "blocks" | awk '{print $2}' | awk -F"," '{print $1}'`
CONNS=`$BITCOIND_RPC getinfo | grep "connections" | awk '{print $2}' | awk -F"," '{print $1}'`
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
  echo "[$DATE] bitcoind's connections is 0: $RPCINFO" >>$LOG
  echo "$RPCINFO"
fi
