SET SQL_MODE = "NO_AUTO_VALUE_ON_ZERO";
SET AUTOCOMMIT = 0;
START TRANSACTION;
SET time_zone = "+00:00";

CREATE TABLE IF NOT EXISTS `shares` (
  `id` bigint(20) UNSIGNED NOT NULL AUTO_INCREMENT,
  `job_id` varchar(255) NOT NULL,
  `pow_hash` varchar(255) NOT NULL,
  `mix_digest` varchar(255) NOT NULL,
  `response` varchar(255) NOT NULL,
  `nonce` bigint(20) UNSIGNED NOT NULL,
  `diff` bigint(20) UNSIGNED NOT NULL,
  `height` bigint(20) UNSIGNED NOT NULL,
  `timestamp` bigint(20) UNSIGNED NOT NULL,
  `ip` varchar(255) NOT NULL,
  `port` smallint(5) UNSIGNED NOT NULL,
  `miner_fullname` varchar(255) NOT NULL,
  `miner_wallet` varchar(255) NOT NULL,
  `miner_user` varchar(255) NOT NULL,
  `miner_worker` varchar(255) NOT NULL,
  `miner_pwd` varchar(255) NOT NULL,
  `pool_name` varchar(255) NOT NULL,
  `pool_url` varchar(255) NOT NULL,
  `pool_user` varchar(255) NOT NULL,
  `pool_worker` varchar(255) NOT NULL,
  `pool_pwd` varchar(255) NOT NULL,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
COMMIT;
