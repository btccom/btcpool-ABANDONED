#!/usr/bin/env php
<?php
require_once __DIR__.'/../lib/init.php';
// PHP syntax for templates
// https://www.php.net/manual/control-structures.alternative-syntax.php
// https://www.php.net/manual/language.basic-syntax.phpmode.php

#
# sserver.cfg generator
#
# @since 2019-09
# @copyright btc.com
#
?>
# is using testnet3
testnet = <?=optionalBool('testnet', false)?>;

sserver = {
  # serverType
  type = "<?=notNullTrim('sserver_type')?>";
  
  ip = "<?=optionalTrim('sserver_ip', '0.0.0.0')?>";
  port = <?=optionalTrim('sserver_port', '3333')?>;

  # should be global unique, range: [1, 255]
  # If the id is 0, try to automatically assign one from zookeeper.
  id = <?=$sserver_id = optionalTrim('sserver_id', '0')?>;
  # The lock path used when automatically assigning an id
  zookeeper_lock_path = "<?=mayOptionalTrim($sserver_id != 0, 'sserver_zookeeper_lock_path')?>";

  # how many seconds between two share submit
  share_avg_seconds = <?=optionalTrim('sserver_share_avg_seconds', '10')?>;

  # the lifetime of a job
  # It should not be too short, otherwise the valid share will be rejected due to job not found.
  max_job_lifetime = <?=optionalTrim('sserver_max_job_lifetime', '300')?>;

  # the job interval
  # sserver will push latest job if there are no new jobs for this interval
  mining_notify_interval = <?=optionalTrim('sserver_mining_notify_interval', '30')?>;

  # default difficulty (hex)
  default_difficulty = "<?=hexPowerOfOptionalTrim('sserver_default_difficulty', '10000')?>";

  # max difficulty (hex)
  max_difficulty = "<?=hexPowerOfOptionalTrim('sserver_max_difficulty', '4000000000000000')?>";

  # min difficulty (hex)
  min_difficulty = "<?=hexPowerOfOptionalTrim('sserver_min_difficulty', '40')?>";

  # Adjust difficulty once every N second
  diff_adjust_period = <?=optionalTrim('sserver_diff_adjust_period', '900')?>;

  # When exiting, the connection will be closed gradually within the specified time.
  # Set to 0 to disable this feature.
  shutdown_grace_period = <?=optionalTrim('sserver_shutdown_grace_period', '3600')?>;

  # Override these in each chain (optionally)
  nicehash = {
    # Set to true if you want to force minimal difficulty for whole sserver
    forced = <?=optionalBool('sserver_nicehash_forced', false, $sserver_nicehash_forced)?>;

    # Fallback value when ZooKeeper is not available
    min_difficulty = "<?=hexPowerOfMayOptionalTrim(!$sserver_nicehash_forced, 'sserver_nicehash_min_difficulty', '80000')?>";

    # Read NiceHash minimal difficulty from this ZooKeeper node
    min_difficulty_zookeeper_path = "<?=mayOptionalTrim(!$sserver_nicehash_forced, 'sserver_nicehash_min_difficulty_zookeeper_path')?>"
  };
  
  #
  # version_mask, uint32_t
  #          2(0x00000002) : allow client change bit 1
  #         16(0x00000010) : allow client change bit 4
  #  536862720(0x1fffe000) : allow client change bit 13 to 28
  #
  #  version_mask = 0;
  #  version_mask = 16;
  #  version_mask = 536862720; // recommended, BIP9 security
  #  ...
  #
  version_mask = <?=optionalTrim('sserver_version_mask', '0')?>;

  # it could be 4 ~ 8
  # it should be 4 if you want proxy stratum jobs with poolwatcher(proxy).cfg
  extra_nonce2_size = <?=optionalTrim('sserver_extra_nonce2_size', '8')?>;

  # Send ShareBitcoinBytesV1 to share_topic to keep compatibility with legacy statshttpd/sharelogger.
  use_share_v1 = <?=optionalBool('sserver_use_share_v1', false)?>;

  # Accepting the PROXY Protocol to get the original IP of the miner from a proxy.
  # <https://docs.nginx.com/nginx/admin-guide/load-balancer/using-proxy-protocol/>
  proxy_protocol = <?=optionalBool('sserver_proxy_protocol', false)?>;

<?php
  $chains = commaSplitTrim('chains');
  $multi_chains = !empty($chains);
?>
  # Mining multi chains (such as BTC and BCH) with the same sserver.
  # Sserver will query zookeeper to find the chain that a user want mining.
  multi_chains = <?=toString($multi_chains)?>;

<?php
if (!$multi_chains):
?>
  # topics
  job_topic = "<?=notNullTrim("sserver_job_topic")?>";
  share_topic = "<?=notNullTrim("sserver_share_topic")?>";
  solved_share_topic = "<?=notNullTrim("sserver_solved_share_topic")?>";
  auxpow_solved_share_topic = "<?=notNullTrim("sserver_auxpow_solved_share_topic")?>"; # auxpow (eg. Namecoin) solved share topic
  rsk_solved_share_topic = "<?=notNullTrim("sserver_rsk_solved_share_topic")?>";
  common_events_topic = "<?=notNullTrim("sserver_common_events_topic")?>";
<?php
endif;
?>

  ########################## dev options #########################

  # if enable simulator, all share will be accepted. for testing
  enable_simulator = <?=optionalBool('sserver_enable_simulator', false)?>;

  # if enable it, all share will make block and submit. for testing
  enable_submit_invalid_block = <?=optionalBool('sserver_enable_submit_invalid_block', false)?>;

  # if enable, difficulty sent to miners is always dev_fixed_difficulty. for development
  enable_dev_mode = <?=optionalBool('sserver_enable_dev_mode', false)?>;

  # difficulty to send to miners. for development
  dev_fixed_difficulty = <?=optionalTrim('sserver_dev_fixed_difficulty', '0.005')?>;
  
  ###################### end of dev options ######################
};

kafka = {
  brokers = "<?=mayOptionalTrim($multi_chains, 'kafka_brokers')?>";
};

zookeeper = {
  brokers = "<?=mayOptionalTrim($sserver_id != 0 && count($chains) < 2, 'zookeeper_brokers')?>";
};

users = {
  list_id_api_url = "<?=mayOptionalTrim($multi_chains, "users_list_id_api_url")?>";

  # Make the user name case insensitive
  case_insensitive = <?=optionalBool('users_case_insensitive', true)?>;

  # The parent node of userName-chainName map in Zookeeper
  zookeeper_userchain_map = "<?=mayOptionalTrim(count($chains) < 2, 'users_zookeeper_userchain_map')?>";

  # remove the suffix appended to the user name
  # example: tiger_eth -> tiger, aaa_bbb_ccc -> aaa_bbb
  strip_user_suffix = <?=optionalBool('users_strip_user_suffix', true)?>;
  user_suffix_separator = "<?=optionalTrim('users_user_suffix_separator', '_')?>";

  # user auto register
  enable_auto_reg = <?=optionalBool('users_enable_auto_reg', false, $users_enable_auto_reg)?>;
  auto_reg_max_pending_users = <?=optionalTrim('users_auto_reg_max_pending_users', '50')?>;
  zookeeper_auto_reg_watch_dir = "<?=mayOptionalTrim(!$users_enable_auto_reg, 'users_zookeeper_auto_reg_watch_dir')?>";

  # Use a single user-selected chain.
  #
  # If true, this sserver will use single_user_name as the reference user for chain switching,
  # ignoring the sub-accounts that are actually authenticated. It takes effect even if
  # single-user mode is not enabled.
  #
  # If false, the chain specified by the sub-account actually authenticated is selected.
  # 
  single_user_chain = <?=optionalBool('users_single_user_chain', false, $users_single_user_chain)?>;

  # Enable single user mode.
  # In this mode, all sub-accounts connected to the sserver will become the worker name prefix for a specified user.
  # Example, a worker "user2.11x20" will become "user1.user2.11x20".
  single_user_mode = <?=optionalBool('users_single_user_mode', false, $users_single_user_mode)?>;
  single_user_name = "<?=mayOptionalTrim(!$users_single_user_mode && !$users_single_user_chain, "users_single_user_name")?>";
  single_user_puid = <?=mayOptionalTrim(!$users_single_user_mode || $multi_chains, "users_single_user_puid", '0')?>;
};

chains = (
<?php
foreach ($chains as $key=>$chain_name):
?>
  {
    name = <?=toString($chain_name)?>;
    users_list_id_api_url = "<?=notNullTrim("chains_${chain_name}_users_list_id_api_url")?>";

    # write last mining notify job send time to file, for monitor
    file_last_notify_time = "<?=optionalTrim("chains_${chain_name}_file_last_notify_time")?>";

    # kafka brokers
    kafka_brokers = "<?=notNullTrim("chains_${chain_name}_kafka_brokers")?>";
    # kafka topics
    job_topic = "<?=notNullTrim("chains_${chain_name}_job_topic")?>";
    share_topic = "<?=notNullTrim("chains_${chain_name}_share_topic")?>";
    solved_share_topic = "<?=notNullTrim("chains_${chain_name}_solved_share_topic")?>";
    common_events_topic = "<?=notNullTrim("chains_${chain_name}_common_events_topic")?>";
    auxpow_solved_share_topic = "<?=notNullTrim("chains_${chain_name}_auxpow_solved_share_topic")?>";
    rsk_solved_share_topic = "<?=notNullTrim("chains_${chain_name}_rsk_solved_share_topic")?>";

    # Specify a chain-based user id for single-user mode
    single_user_puid = <?=mayOptionalTrim(!$users_single_user_mode, "chains_${chain_name}_single_user_puid", '0')?>;
  }<?=separator(',', $chains, $key)?>

<?php
endforeach;
?>
);

prometheus = {
  # whether prometheus exporter is enabled
  enabled = <?=optionalBool('prometheus_enabled', true)?>;
  # address for prometheus exporter to bind
  address = "<?=optionalTrim('prometheus_address', '0.0.0.0')?>";
  # port for prometheus exporter to bind
  port = <?=optionalTrim('prometheus_port', '9100')?>;
  # path of the prometheus exporter url
  path = "<?=optionalTrim('prometheus_path', '/metrics')?>";
};

management = {
  enabled = <?=optionalBool('management_enabled', true, $management_enabled)?>; # default: true

  kafka_brokers = "<?=mayOptionalTrim(!$management_enabled, "management_kafka_brokers")?>"; # "10.0.0.1:9092,10.0.0.2:9092,..."
  controller_topic = "<?=mayOptionalTrim(!$management_enabled, "management_controller_topic")?>";
  processor_topic = "<?=mayOptionalTrim(!$management_enabled, "management_processor_topic")?>";

  auto_switch_chain = <?=optionalBool('management_auto_switch_chain', true)?>;
};

# Share jobs with the main pool, but with different coinbase information and addresses.
subpool = {
  enabled = <?=optionalBool('subpool_enabled', false)?>; # default: false
  name = "<?=optionalTrim('subpool_name', '')?>";
  ext_user_id = <?=optionalTrim('subpool_ext_user_id', '0')?>; # Optional, reserved for data analysis. It should < 0 (preventing it is no different from single-user mode).
};
