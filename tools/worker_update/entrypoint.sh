#!/bin/bash
# Generate configuration files from environment variables and start processes
set -e

/work/cfg-generator/tools/worker_update.php >/tmp/worker_update.cfg

exec worker_update -c /tmp/worker_update.cfg "$@"
