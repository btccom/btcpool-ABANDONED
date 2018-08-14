--
-- 2018-08-08
-- rename `found_blocks.hash` as `found_blocks.hash_no_nonce`,
-- then add a new field named `found_blocks.hash`.
--
ALTER TABLE `found_blocks`
DROP INDEX `hash`,
CHANGE `hash` `hash_no_nonce` char(66) NOT NULL,
ADD `hash` char(66) DEFAULT '' NOT NULL AFTER `ref_uncles`,
ADD INDEX `hash`(`hash`),
ADD INDEX `hash_no_nonce`(`hash_no_nonce`),
ADD UNIQUE INDEX `block_hash`(`hash_no_nonce`,`hash`);
