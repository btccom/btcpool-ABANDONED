#!/bin/bash
# Generate configuration files from environment variables and start processes
set -e

/work/cfg-generator/tools/sharelog_to_parquet.php >/tmp/sharelog_to_parquet.cfg

exec sharelog_to_parquet -c /tmp/sharelog_to_parquet.cfg "$@"
