#!/bin/bash
# automatic integration test for mining Bitcoin

export BASE_DIR="$( cd "$( dirname "$0" )" && pwd )"
export STDOUT="/tmp/btcpool-test-btc-stdout.log"
export LOGFILE="/tmp/btcpool-test-btc-result.log"

echo -n >$STDOUT

echo "View logs:"
echo "    tail -F $STDOUT"
echo "View result:"
echo "    tail -F $LOGFILE"
echo

if [ "x$1" != "x" ]; then
    /bin/bash ../testosteron/testosteron run $BASE_DIR/testcase/$1*
else
    /bin/bash ../testosteron/testosteron rundir $BASE_DIR/testcase
fi

echo "clear docker compose..."
cd "$BASE_DIR"
docker-compose down
