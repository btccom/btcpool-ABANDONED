-- Adminer 4.2.4 MySQL dump

SET NAMES utf8;
SET time_zone = '+00:00';
SET foreign_key_checks = 0;
SET sql_mode = 'NO_AUTO_VALUE_ON_ZERO';

DROP TABLE IF EXISTS `stats_pool_day`;
CREATE TABLE `stats_pool_day` (
  `day` int(11) NOT NULL,
  `share_accept` bigint(20) NOT NULL DEFAULT '0',
  `share_reject` bigint(20) NOT NULL DEFAULT '0',
  `reject_rate` double NOT NULL DEFAULT '0',
  `score` decimal(35,25) NOT NULL DEFAULT '0.0000000000000000000000000',
  `earn` bigint(20) NOT NULL DEFAULT '0',
  `lucky` double NOT NULL DEFAULT '0',
  `created_at` timestamp NULL DEFAULT NULL,
  `updated_at` timestamp NULL DEFAULT NULL,
  UNIQUE KEY `day` (`day`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;


DROP TABLE IF EXISTS `stats_pool_hour`;
CREATE TABLE `stats_pool_hour` (
  `hour` int(11) NOT NULL,
  `share_accept` bigint(20) NOT NULL DEFAULT '0',
  `share_reject` bigint(20) NOT NULL DEFAULT '0',
  `reject_rate` double NOT NULL DEFAULT '0',
  `score` decimal(35,25) NOT NULL DEFAULT '0.0000000000000000000000000',
  `earn` bigint(20) NOT NULL DEFAULT '0',
  `created_at` timestamp NULL DEFAULT NULL,
  `updated_at` timestamp NULL DEFAULT NULL,
  UNIQUE KEY `hour` (`hour`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;


DROP TABLE IF EXISTS `stats_users_day`;
CREATE TABLE `stats_users_day` (
  `puid` int(11) NOT NULL,
  `day` int(11) NOT NULL,
  `share_accept` bigint(20) NOT NULL DEFAULT '0',
  `share_reject` bigint(20) NOT NULL DEFAULT '0',
  `reject_rate` double NOT NULL DEFAULT '0',
  `score` decimal(35,25) NOT NULL DEFAULT '0.0000000000000000000000000',
  `earn` bigint(20) NOT NULL DEFAULT '0',
  `created_at` timestamp NULL DEFAULT NULL,
  `updated_at` timestamp NULL DEFAULT NULL,
  UNIQUE KEY `puid_day` (`puid`,`day`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;


DROP TABLE IF EXISTS `stats_users_hour`;
CREATE TABLE `stats_users_hour` (
  `puid` int(11) NOT NULL,
  `hour` int(11) NOT NULL,
  `share_accept` bigint(20) NOT NULL DEFAULT '0',
  `share_reject` bigint(20) NOT NULL DEFAULT '0',
  `reject_rate` double NOT NULL DEFAULT '0',
  `score` decimal(35,25) NOT NULL DEFAULT '0.0000000000000000000000000',
  `earn` bigint(20) NOT NULL DEFAULT '0',
  `created_at` timestamp NULL DEFAULT NULL,
  `updated_at` timestamp NULL DEFAULT NULL,
  UNIQUE KEY `puid_hour` (`puid`,`hour`),
  KEY `hour` (`hour`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;


DROP TABLE IF EXISTS `stats_workers_day`;
CREATE TABLE `stats_workers_day` (
  `puid` int(11) NOT NULL,
  `worker_id` bigint(20) NOT NULL,
  `day` int(11) NOT NULL,
  `share_accept` bigint(20) NOT NULL DEFAULT '0',
  `share_reject` bigint(20) NOT NULL DEFAULT '0',
  `reject_rate` double NOT NULL DEFAULT '0',
  `score` decimal(35,25) NOT NULL DEFAULT '0.0000000000000000000000000',
  `earn` bigint(20) NOT NULL DEFAULT '0',
  `created_at` timestamp NULL DEFAULT NULL,
  `updated_at` timestamp NULL DEFAULT NULL,
  UNIQUE KEY `puid_worker_id_day` (`puid`,`worker_id`,`day`),
  KEY `day` (`day`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;


DROP TABLE IF EXISTS `stats_workers_hour`;
CREATE TABLE `stats_workers_hour` (
  `puid` int(11) NOT NULL,
  `worker_id` bigint(20) NOT NULL,
  `hour` int(11) NOT NULL,
  `share_accept` bigint(20) NOT NULL DEFAULT '0',
  `share_reject` bigint(20) NOT NULL DEFAULT '0',
  `reject_rate` double NOT NULL DEFAULT '0',
  `score` decimal(35,25) NOT NULL DEFAULT '0.0000000000000000000000000',
  `earn` bigint(20) NOT NULL DEFAULT '0',
  `created_at` timestamp NULL DEFAULT NULL,
  `updated_at` timestamp NULL DEFAULT NULL,
  UNIQUE KEY `puid_worker_id_hour` (`puid`,`worker_id`,`hour`),
  KEY `hour` (`hour`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;


-- 2016-09-05 08:04:07
