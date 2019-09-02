#!/usr/bin/env php
<?php
require_once __DIR__.'/../lib/init.php';
// PHP syntax for templates
// https://www.php.net/manual/control-structures.alternative-syntax.php
// https://www.php.net/manual/language.basic-syntax.phpmode.php

#
# statshttpd.cfg generator
#
# @since 2019-09
# @copyright btc.com
#
?>
kafka = {
  brokers = "<?=notNullTrim('kafka_brokers')?>";
};

statshttpd = {
  chain_type = "<?=notNullTrim('statshttpd_chain_type')?>";
  share_topic = "<?=notNullTrim('statshttpd_share_topic')?>";

  # Writing miner name to MySQL via statshttpd may be too slow.
  # You can consider using the standalone worker_update tool (in tools/worker_update).
  update_worker_name = <?=optionalBool('statshttpd_update_worker_name', true, $statshttpd_update_worker_name)?>;
  
  # common events topic
  # example: miner connected, miner disconnected, ...
  common_events_topic = "<?=mayOptionalTrim(!$statshttpd_update_worker_name, "statshttpd_common_events_topic")?>";
  
  ip = "<?=optionalTrim('statshttpd_ip', '0.0.0.0')?>";
  port = <?=optionalTrim('statshttpd_port', '8080')?>;

  # interval seconds, flush workers data into database
  # it's very fast because we use insert statement with multiple values and
  # merge table when flush data to DB. we have test mysql, it could flush
  # 25,000 workers into DB in about 1.7 seconds.
  flush_db_interval = <?=optionalTrim('statshttpd_flush_db_interval', '30')?>;

  # write last db flush time to file
  file_last_flush_time = "<?=optionalTrim('statshttpd_file_last_flush_time')?>";

  # write mining workers' info to mysql database
  use_mysql = <?=optionalBool('statshttpd_use_mysql', true, $statshttpd_use_mysql)?>;
  # write mining workers' info to redis
  use_redis = <?=optionalBool('statshttpd_use_redis', false, $statshttpd_use_redis)?>;

  # Used to initialize the offset of kafka consumers
  # If you want to reduce the time spent on consumption history after statshttpd restarts,
  # or if your online workers are too large to be inaccurate for accept_1h after restarts,
  # adjust this value to be close to your actual online workers.
  expected_online_workers = <?=optionalTrim('statshttpd_expected_online_workers', '100000')?>;

  # Whether stale shares are accepted
  accept_stale = <?=optionalBool('statshttpd_accept_stale', false)?>;
};

users = {
  # Enable single user mode.
  # Count shares from sserver that enabled single-user mode.
  single_user_mode = <?=optionalBool('users_single_user_mode', false, $users_single_user_mode)?>;
  single_user_puid = <?=mayOptionalTrim(!$users_single_user_mode, "users_single_user_puid", '0')?>;
};

<?php if ($statshttpd_use_mysql): ?>
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
<?php endif; ?>

<?php if ($statshttpd_use_redis): ?>
#
# pool redis
#
redis = {
  host = "<?=notNullTrim('redis_host')?>";
  port = <?=optionalTrim('redis_port', '6379')?>;
  password = "<?=optionalTrim('redis_password')?>"; # keep it empty if no password auth required

  # keys:
  #     worker: HGETALL    "{key_prefix}mining_workers/pu/{puid}/wk/{workerid}"
  #             PSUBSCRIBE "{key_prefix}mining_workers/pu/{puid}/wk/*"
  #     user:   HGETALL    "{key_prefix}mining_workers/pu/{puid}/all"
  #             PSUBSCRIBE "{key_prefix}mining_workers/pu/*/all"
  key_prefix = "<?=optionalTrim('redis_key_prefix')?>";

  # expiration seconds of every key (0 for unlimited):
  key_expire = <?=optionalTrim('redis_key_expire', '0')?>;

  # policy about publish a message to the key's subscriber when its data updated.
  # 0: no publish
  # 1: only users
  # 2: only workers
  # 3: both users and workers
  publish_policy = <?=optionalTrim('redis_publish_policy', '0')?>;

  # policy about creating some indexes to redis so we can sort and page the records.
  # it is a bitset, add all you desired values and fill the summation.
  #
  #    1: accept_1m          ZADD "{key_prefix}:mining_workers/pu/{puid}/sort/accept_1m"       {accept_1m}       {workerid}
  #    2: accept_5m          ZADD "{key_prefix}:mining_workers/pu/{puid}/sort/accept_5m"       {accept_5m}       {workerid}
  #    4: accept_15m         ZADD "{key_prefix}:mining_workers/pu/{puid}/sort/accept_15m"      {accept_15m}      {workerid}
  #    8: reject_15m         ZADD "{key_prefix}:mining_workers/pu/{puid}/sort/reject_15m"      {reject_15m}      {workerid}
  #   16: accept_1h          ZADD "{key_prefix}:mining_workers/pu/{puid}/sort/accept_1h"       {accept_1h}       {workerid}
  #   32: reject_1h          ZADD "{key_prefix}:mining_workers/pu/{puid}/sort/reject_1h"       {reject_1h}       {workerid}
  #   64: accept_count       ZADD "{key_prefix}:mining_workers/pu/{puid}/sort/accept_count"    {accept_count}    {workerid}
  #  128: last_share_ip      ZADD "{key_prefix}:mining_workers/pu/{puid}/sort/last_share_ip"   {last_share_ip}   {workerid}
  #  256: last_share_time    ZADD "{key_prefix}:mining_workers/pu/{puid}/sort/last_share_time" {last_share_time} {workerid}
  #  512: worker_name        ZADD "{key_prefix}:mining_workers/pu/{puid}/sort/last_share_time" {worker_name}     {workerid}
  # 1024: miner_agent        ZADD "{key_prefix}:mining_workers/pu/{puid}/sort/last_share_time" {miner_agent}     {workerid}
  #
  # example:
  # 276 = 4 + 16 + 256 = accept_15m, accept_1h and last_share_time
  #
  index_policy = <?=optionalTrim('redis_index_policy', '0')?>;

  # write redis with multiple threads.
  # try increasing the value to solve the performance problem.
  concurrency = <?=optionalTrim('redis_concurrency', '1')?>;
};
<?php endif; ?>
