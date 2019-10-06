#!/bin/bash
# Generate configuration files from environment variables and start processes
set -e
exec stratum_log_proxy "$@"
