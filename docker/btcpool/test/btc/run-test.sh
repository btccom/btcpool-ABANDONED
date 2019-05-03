#!/bin/bash
# automatic integration test for mining Bitcoin

export BASE_DIR="$( cd "$( dirname "$0" )" && pwd )"
export STDOUT="/tmp/btcpool-test-btc-stdout.log"
export LOGFILE="/tmp/btcpool-test-btc-result.log"
export ARG_STORE="/tmp/btcpool-test-btc-argstore.conf"

source $ARG_STORE 2>/dev/null

echo -n >$STDOUT
echo -n >$ARG_STORE

# stored args
if [ "x$APT_MIRROR_URL" != "x" ]; then
    echo "export APT_MIRROR_URL='$APT_MIRROR_URL'" >> $ARG_STORE
fi

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
