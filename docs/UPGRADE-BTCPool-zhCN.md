升级BTCPool
=====================

* [English 英语](./UPGRADE-BTCPool.md)
* 简体中文 (当前)

# 升级到 BTCPool 2.3.0

为了更好的可扩展性，BTCPool 2.3.0 改为在传输和存储 share 的过程中使用 ProtoBuf。

这是一个与之前的部署不兼容的更新。所以为了升级到该版本，你需要做一些额外的操作。

## 有什么问题？

BTCPool 2.3.0 在以下方面与之前的部署不兼容：

1. 新的`sserver`将产生 ProtoBuf 格式的 share，旧的`sharelogger`和`statshttpd`无法处理。
2. 旧版本的sharelog文件格式与新版本的不兼容。如果你直接升级`sharelogger`，不清理掉以前的sharelog文件，文件就会以新的格式继续写入。这将导致旧版本和新版本的`slparser`都无法读取该文件。
3. 如果简单的移除旧版本的sharelog文件，新产生的sharelog文件将是不完整的。因为kafka中记录了`share_topic`的消费进度。只有更改`sharelogger.cfg`中的`kafka_group_id`才能重置该进度。

BTCPool 2.3.0 在以下方面与旧的部署兼容：
1. 新版本的`statshttpd`和`sharelogger`可以处理kafka中旧版本的share。
2. 因为原因1，你可以混用新旧两个版本的`sserver`，不会出问题。
3. 除了`share_topic`中消息和sharelog文件格式的更改之外，该版本不存在其他不兼容更改。

## 推荐的升级步骤

1. 运行一个新的`statshttpd`，写入和旧版相同的数据库。确认新版工作正常后停止旧版。
2. 运行一个新的`sharelogger`，注意要给它一个完全不同的`kafka_group_id`，并指定一个新的`data_dir`。它会从`share_topic`中的第一个消息开始重建sharelog二进制文件。
3. 等待今天的sharelog文件创建好后，运行新的`slparser`来读取它。
4. 确认新的`slparser`工作正常后，停止旧版。
5. 现在可以开始部署新版的`sserver`，或者把旧版`sserver`升级为新版了。
