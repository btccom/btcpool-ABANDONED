# This script is to be sourced with $BASE_IMAGE set

export BASE_DIR="$( cd "$( dirname "$0" )" && pwd )"
export TEST_SUITE="$(basename "$BASE_DIR")"
export STDOUT="/tmp/btcpool-test-$TEST_SUITE-stdout.log"
export LOGFILE="/tmp/btcpool-test-$TEST_SUITE-result.log"
export ARG_STORE="/tmp/btcpool-test-$TEST_SUITE-argstore.conf"

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
    TESTOSTERON_EXIT_CODE=$?
else
    /bin/bash ../testosteron/testosteron rundir $BASE_DIR/testcase
    TESTOSTERON_EXIT_CODE=$?
fi

if [ -f "$BASE_DIR/docker-compose.yml" ]; then
    echo "clear docker compose..."
    cd "$BASE_DIR"
    docker-compose down
fi

exit $TESTOSTERON_EXIT_CODE
