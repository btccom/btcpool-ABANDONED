#!/bin/bash
# build docker images of BTCPool

TITLE=""
BASE_IMAGE=""
BUILD_JOBS="$(nproc)"
GIT_DESCRIBE="$(git describe --tag --long)"

while getopts 't:b:j:' c
do
  case $c in
    t) TITLE="$OPTARG" ;;
    b) BASE_IMAGE="$OPTARG" ;;
    j) BUILD_JOBS="$OPTARG" ;;
  esac
done

if [ "x$TITLE" = "x" ] || [ "x$BASE_IMAGE" = "x" ]; then
	echo "Usage: $0 -t <image-title> -b <base-image> -j<build-jobs>"
	echo "Example: $0 -t btccom/btcpool-btc -b btccom/btcpool_build:btc-0.16.3 -j$(nproc)"
	exit
fi

docker build -t "$TITLE" -f Dockerfile --build-arg BASE_IMAGE="$BASE_IMAGE" --build-arg BUILD_JOBS="$BUILD_JOBS" --build-arg GIT_DESCRIBE="$GIT_DESCRIBE" ../../..

