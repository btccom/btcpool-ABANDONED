Upgrade BTCPool
=====================

* English (this)
* [Chinese 简体中文](./UPGRADE-BTCPool-zhCN.md)

# Upgrade BTCPool to 2.3.0

For better extendibility, BTCPool 2.3.0 changed the transfer and storage format of shares to ProtoBuf.

This is an update that is not compatible with previous deployments. Therefore, upgrading to this version requires additional work.

## What's the problem

BTCPool 2.3.0 is not compatible with previous versions in three places:

1. The new version of `sserver` will send ProtoBuf encoded shares to kafka. The old version of `sharelogger` and `statshttpd` cannot handle these shares and will ignore them.
2. Older versions of the sharelog binary file are not compatible with the new version. If you update the `sharelogger` directly and do not clean up the written sharelog file, the file with old formatting will appened with new formatting. This will cause any `slparser (both the new and the old) to fail to read the file.
3. If you simply remove the old version of the sharelog file, the newly generated sharelog file will be incomplete. This is because kafka retains the consumption offset of `share_topic`. You must change the `kafka_group_id` in your `sharelogger.cfg` to reset the offset.

BTCPool 2.3.0 is compatible with old deployments in these places:
1. Older versions of share messages in kafka can be processed normally by the new version of `statshttpd` and `sharelogger`.
2. For place 1, you can mix the new and old versions of `sserver` without causing problems.
3. There are no other incompatible changes in this version other than the format change of the message in `share_topic` and the sharelog binary file.

## Recommended upgrade steps

1. Run a new version of `statshttpd` and write the same database as the old version. After determining that the new version is working properly, stop the old.
2. Run a new version of `sharelogger`, taking care to set a completely different `kafka_group_id` and a new `data_dir`. It will rebuild the sharelog binary files from the first message in `share_topic`. 
3. After the new version of the today's sharelog file is created, run a new version of slparser to read these sharelogs.
4. Confirm that the new version of `slparser` is working properly, then stop the old.
5. Start deploying a new version of `sserver` or upgrading the old version with the new.
