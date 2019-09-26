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

--
-- 2018-08-23
-- Purpose:
--    To solve the issue of inserting two duplicate records (one has empty `hash`) 
--    when only a part of nodes fail to submit with eth_submitWorkDetail and
--    submit success with eth_submitWork.
-- Actions:
--     add a new field `nonce`.
--     add a new unique index (`hash_no_nonce`, `nonce`).
--     remove the old unique index (`hash_no_nonce`, `hash`).
--
ALTER TABLE `found_blocks`
ADD `nonce` char(18) NOT NULL AFTER `hash_no_nonce`,
ADD UNIQUE INDEX `unique_block`(`hash_no_nonce`,`nonce`),
DROP INDEX `hash_no_nonce`,
DROP INDEX `block_hash`;
