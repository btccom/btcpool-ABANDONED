#!/bin/bash
# automatic integration test for mining Bitcoin

export BASE_DIR="$( cd "$( dirname "$0" )" && pwd )"
export BASE_IMAGE=btccom/btcpool_build:btc-0.16.3

# Generate a block for bitcoind to prevent the getblocktemplate call from failing
export GENERATE_BLOCK=1

source ../common/run-manually.sh
