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

if [ "x$*" != "x" ]; then
    TESTCASES=("$@")
    PATHS=()
    for testcase in ${TESTCASES[@]}; do
        PATHS+=($BASE_DIR/testcase/$testcase*)
    done
    /bin/bash ../testosteron/testosteron runsome ${PATHS[@]}
else
    /bin/bash ../testosteron/testosteron rundir $BASE_DIR/testcase
fi

echo "clear docker compose..."
cd "$BASE_DIR"
docker-compose down
