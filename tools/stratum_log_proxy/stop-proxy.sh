#!/bin/sh
# stop proxy
set -e

cd "$( dirname "$0" )"
docker-compose stop
