#!/bin/bash

# Wait for the container to run
# arg1: container name
# arg2...: args of wait-for-it.sh
WAIT_FOR_IT() {
    container_name="$1"
    shift 1
    while ! docker inspect -f '{{.State.Running}}' "$container_name" &>/dev/null; do
        sleep 1
    done
    docker exec "$container_name" /wait-for-it.sh "$@"
}
