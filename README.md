Pool Components
==================

* GbtMaker
  * 从bitcoind通过RPC调用GetBlockTemplate获取，并发送到MQ
  * MQ消息类型
    * 接收：无
    * 发送：RawGbt
* JobMaker
  * 从MQ获取`rawgbt`消息，选择最佳gbt并解析成Stratum Job，发送至MQ
  * MQ消息类型
    * 接收：RawGbt
    * 发送：StratumJob
* BlockMaker
  * 监控出块Share，并根据对应Job组合成新块，并提交至bitcoind
  * MQ消息类型
    * 接收：RawGbt, StratumJob, SolvedShare
    * 发送：无


Library
============

Basic:

* openssl
* libconfig++

```
apt-get update
apt-get install -y build-essential autotools-dev libtool autoconf automake pkg-config cmake
apt-get install -y openssl libssl-dev libcurl4-openssl-dev libconfig++-dev libboost-all-dev
apt-get install -y libmysqlclient-dev
```

zmq-v4.1.5

```
cd /root/source
wget https://github.com/zeromq/zeromq4-1/releases/download/v4.1.5/zeromq-4.1.5.tar.gz
tar zxvf zeromq-4.1.5.tar.gz
cd zeromq-4.1.5
./autogen.sh && ./configure && make -j4
make check && make install && ldconfig
```

glog-v0.3.4

```
cd /root/source
wget https://github.com/google/glog/archive/v0.3.4.tar.gz
tar zxvf v0.3.4.tar.gz
cd glog-0.3.4
./configure && make && make install
```

librdkafka-v0.9.1

```
apt-get install -y zlib1g zlib1g-dev
cd /root/source
wget https://github.com/edenhill/librdkafka/archive/0.9.1.tar.gz
tar zxvf 0.9.1.tar.gz
cd librdkafka-0.9.1
./configure && make && make install
```

更新动态库：

`vim /etc/ld.so.conf`添加一行：`/usr/local/lib`，并运行：`ldconfig`。

Create Topics
=============

示例，三个备份，一个分区：

```
> ./bin/kafka-topics.sh --create --zookeeper 10.47.222.193:2181,10.25.244.67:2181,10.24.198.217:2181 --replication-factor 3 --partitions 1 --topic RawGbt
```

Kafka Settings
===============

Support Big Message:

```
#
# brokers: config/server.properties
#
message.max.bytes=20000000
replica.fetch.max.bytes=30000000
```

Message Format
==============

Topic: RawGbt

```
type   : text/json
format : {"created_at_ts": <uint32>,
          "block_template_base64": "<json string>"}
          
# created_at_ts         : 本消息创建时间，可以当做是bitcoind rpc call的时间
# block_template_base64 : bitcoind rpc返回的字符串，base64编码
```
