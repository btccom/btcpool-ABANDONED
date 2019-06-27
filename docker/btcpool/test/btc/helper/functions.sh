#!/bin/bash

# Wait for the container to run
# arg1: container name
WAIT_FOR_CONTAINER() {
    container_name="$1"
    while ! docker inspect -f '{{.State.Running}}' "$container_name" &>/dev/null; do
        sleep 1
    done
}

# Wait for the port be listened then run a command
# arg1: container name
# arg2...: args of wait-for-it.sh
WAIT_FOR_IT() {
    container_name="$1"
    shift 1
    WAIT_FOR_CONTAINER "$container_name"
    docker exec "$container_name" /wait-for-it.sh "$@"
}

# Wait for the port be listened without creating a connection
# Use this function to wait for the sserver listen port to avoid initial session id changes.
# arg1: container name
# arg2: port
WAIT_FOR_PORT() {
    container_name="$1"
    port="$2"
    WAIT_FOR_CONTAINER "$container_name"
    while [ "$(docker exec "$container_name" netstat -lntp | grep ":$port" | wc -l)" = "0" ]; do
        echo "waiting for port $port..."
        sleep 1
    done
    echo "port $port is ready"
}
