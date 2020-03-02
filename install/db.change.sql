--
-- 2017-04-25
-- add `accept_1h`, `reject_1h` to table `mining_workers`
--
ALTER TABLE `mining_workers`
add `accept_1h` BIGINT default 0 NOT NULL after `reject_15m`,
add `reject_1h` BIGINT default 0 NOT NULL after `accept_1h`;

--
-- 2017-09-13
-- set DEFAULT '0' to field `is_orphaned` in table `found_blocks` and `found_nmc_blocks`
-- increase the length of `worker_full_name` from 20 to 50
--
alter table `found_blocks` change `is_orphaned` `is_orphaned` tinyint(4) NOT NULL DEFAULT '0';
alter table `found_nmc_blocks` change `is_orphaned` `is_orphaned` tinyint(4) NOT NULL DEFAULT '0';
alter table `found_blocks` change `worker_full_name` `worker_full_name` varchar(50) NOT NULL;

--
-- 2019-07-05
-- add `stale_15m`, `stale_1h`, `reject_detail_15m`, `reject_detail_1h` to table `mining_workers`
--
ALTER TABLE `mining_workers`
add `stale_15m` BIGINT default 0 NOT NULL after `accept_15m`,
add `stale_1h` BIGINT default 0 NOT NULL after `accept_1h`,
add `reject_detail_15m` varchar(255) default '' NOT NULL after `reject_15m`,
add `reject_detail_1h` varchar(255) default '' NOT NULL after `reject_1h`;

--
-- 2019-07-06
-- add `share_stale`, `reject_detail` to table `stats_*`
--
ALTER TABLE `stats_pool_day`
add `share_stale` BIGINT default 0 NOT NULL after `share_accept`,
add `reject_detail` varchar(255) default '' NOT NULL after `share_reject`;

ALTER TABLE `stats_pool_hour`
add `share_stale` BIGINT default 0 NOT NULL after `share_accept`,
add `reject_detail` varchar(255) default '' NOT NULL after `share_reject`;

ALTER TABLE `stats_users_day`
add `share_stale` BIGINT default 0 NOT NULL after `share_accept`,
add `reject_detail` varchar(255) default '' NOT NULL after `share_reject`;

ALTER TABLE `stats_users_hour`
add `share_stale` BIGINT default 0 NOT NULL after `share_accept`,
add `reject_detail` varchar(255) default '' NOT NULL after `share_reject`;

ALTER TABLE `stats_workers_day`
add `share_stale` BIGINT default 0 NOT NULL after `share_accept`,
add `reject_detail` varchar(255) default '' NOT NULL after `share_reject`;

ALTER TABLE `stats_workers_hour`
add `share_stale` BIGINT default 0 NOT NULL after `share_accept`,
add `reject_detail` varchar(255) default '' NOT NULL after `share_reject`;


--
-- 2019-08-07
-- add `chain_name`, `submit_response` to table `found_nmc_blocks`
--
ALTER TABLE `found_nmc_blocks`
add `chain_name` varchar(20) NOT NULL default '' after `is_orphaned`,
add `submit_response` varchar(255) NOT NULL default '' after `is_orphaned`;


-- 2020-03-02
-- Increase the length of `mining_workers`.`worker_name` to 50 bytes and add an index on it.
ALTER TABLE `mining_workers` CHANGE `worker_name` `worker_name` VARCHAR(50);
ALTER TABLE `mining_workers` ADD INDEX `puid_worker_name`(`puid`, `worker_name`);
