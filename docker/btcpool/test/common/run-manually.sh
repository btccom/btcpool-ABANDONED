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

cd "$BASE_DIR/testcase"
bash ./0000-build-image

docker-compose up --build $@
