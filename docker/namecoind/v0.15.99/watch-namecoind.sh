#! /bin/bash
#
# namecoind getauxblock watcher shell
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

NAMECOIND_RPC="namecoin-cli "

SUCCESS=`$NAMECOIND_RPC getauxblock | grep "previousblockhash" | wc -l`

if [[ $SUCCESS -ne 1 ]]; then
  # stop namecoind, supervisor will start it again
  echo "got error, restart namecoind..."
  $NAMECOIND_RPC stop
fi

