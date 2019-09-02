#!/usr/bin/env php
<?php
require_once __DIR__.'/../lib/init.php';
// PHP syntax for templates
// https://www.php.net/manual/control-structures.alternative-syntax.php
// https://www.php.net/manual/language.basic-syntax.phpmode.php

#
# slparser.cfg generator
#
# @since 2019-09
# @copyright btc.com
#
?>
testnet = <?=optionalBool('testnet', false)?>; # using consensus (block reward rules) of bitcoin testnet

slparserhttpd = {
  ip = "<?=optionalTrim('slparserhttpd_ip', '0.0.0.0')?>";
  port = <?=optionalTrim('slparserhttpd_port', '8081')?>;

  # interval seconds, flush stats data into database
  # it's very fast because we use insert statement with multiple values and
  # merge table when flush data to DB. we have test mysql, it could flush
  # 50,000 itmes into DB in about 2.5 seconds.
  flush_db_interval = <?=optionalTrim('slparserhttpd_flush_db_interval', '30')?>;
};

sharelog = {
  chain_type = "<?=notNullTrim('sharelog_chain_type')?>";
  data_dir = "<?=notNullTrim('sharelog_data_dir')?>";

  # Enable single user mode.
  # Count shares from sserver that enabled single-user mode.
  single_user_mode = <?=optionalBool('sharelog_single_user_mode', false, $sharelog_single_user_mode)?>;
  single_user_puid = <?=mayOptionalTrim(!$sharelog_single_user_mode, 'sharelog_single_user_puid', '0')?>;
};

#
# pool mysql db: table.stats_xxxx
#
pooldb = {
  host = "<?=notNullTrim('pooldb_host')?>";
  port = <?=optionalTrim('pooldb_port', '3306')?>;
  username = "<?=notNullTrim('pooldb_username')?>";
  password = "<?=notNullTrim('pooldb_password')?>";
  dbname = "<?=notNullTrim('pooldb_dbname')?>";
};
