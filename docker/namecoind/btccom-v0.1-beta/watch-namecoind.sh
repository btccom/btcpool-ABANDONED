#! /bin/bash
#
# namecoind createauxblock watcher shell
#
# this crontab shell is try to fix issue:
#   https://github.com/namecoin/namecoin-core/issues/50#issuecomment-245571582
#
# @copyright btc.com
# @author Zhibiao Pan
# @since 2016-09
#
SROOT=$(cd $(dirname "$0"); pwd)
cd $SROOT

NAMECOIND_RPC="namecoin-cli createauxblock N59bssPo1MbK3khwPELTEomyzYbHLb59uY"

SUCCESS=`$NAMECOIND_RPC | grep "previousblockhash" | wc -l`

if [[ $SUCCESS -ne 1 ]]; then
  IS_DOWNLOADING=`$NAMECOIND_RPC 2>&1 | grep "is downloading blocks" | wc -l` 
  if [[ $IS_DOWNLOADING -eq 1 ]]; then
    echo "namecoind is downloading blocks, exit"
    exit
  fi

  # stop namecoind, supervisor will start it again
  echo "got error, restart namecoind..."
  $NAMECOIND_RPC stop
fi

