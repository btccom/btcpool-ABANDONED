#!/usr/bin/env php
<?php
require_once __DIR__.'/../lib/init.php';
// PHP syntax for templates
// https://www.php.net/manual/control-structures.alternative-syntax.php
// https://www.php.net/manual/language.basic-syntax.phpmode.php

#
# kafka repeater cfg
#
# @since 2019-09
# @copyright btc.com
#
?>
sharelog = {
  chain_type = "<?=optionalTrim('sharelog_chain_type', 'BTC')?>";
  data_dir = "<?=notNullTrim('sharelog_data_dir')?>";
};

users = {
  # Enable single user mode.
  # Count shares from sserver that enabled single-user mode.
  single_user_mode = <?=optionalBool('users_single_user_mode', false, $users_single_user_mode)?>;
  single_user_puid = <?=mayOptionalTrim(!$users_single_user_mode, 'users_single_user_puid', '0')?>;
};

parquet = {
  data_dir = "<?=notNullTrim('parquet_data_dir')?>";
};
