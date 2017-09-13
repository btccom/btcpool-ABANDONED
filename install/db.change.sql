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
