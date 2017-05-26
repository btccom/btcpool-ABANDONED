--
-- 2017-04-25
-- add `accept_1h`, `reject_1h` to table `mining_workers`
--
ALTER TABLE `mining_workers`
add `accept_1h` BIGINT default 0 NOT NULL after `reject_15m`,
add `reject_1h` BIGINT default 0 NOT NULL after `accept_1h`;
