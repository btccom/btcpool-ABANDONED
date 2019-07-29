#!/bin/bash
# Generate configuration files from environment variables and start processes
set -e

/work/cfg-generator/tools/kafka_repeater.php >/tmp/kafka_repeater.cfg

exec kafka_repeater -c /tmp/kafka_repeater.cfg "$@"
