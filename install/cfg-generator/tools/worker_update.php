#!/usr/bin/env php
<?php
require_once __DIR__.'/../lib/init.php';
// PHP syntax for templates
// https://www.php.net/manual/control-structures.alternative-syntax.php
// https://www.php.net/manual/language.basic-syntax.phpmode.php

#
# kafka repeater cfg
#
# @since 2019-07
# @copyright btc.com
#
?>

kafka = {
    brokers = "<?=notNullTrim('kafka_brokers')?>";
    common_events_topic = "<?=notNullTrim('kafka_common_events_topic')?>";
    # Used to record progress / offsets.
    # Change it to reset the progress (will forward from the beginning).
    # The two repeater cannot have the same group id, otherwise the result is undefined.
    group_id = "<?=notNullTrim('kafka_group_id')?>";
};

#
# pool mysql db, table: mining_workers
#
pooldb = {
  host = "<?=notNullTrim('pooldb_host')?>";
  port = <?=optionalTrim('pooldb_port', '3306')?>;
  username = "<?=notNullTrim('pooldb_username')?>";
  password = "<?=notNullTrim('pooldb_password')?>";
  dbname = "<?=notNullTrim('pooldb_dbname')?>";
};

# Logging options
log = {
    repeated_number_display_interval = <?=optionalTrim('log_repeated_number_display_interval', '10')?>; # seconds
};
