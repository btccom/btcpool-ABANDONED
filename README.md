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

* openssl
* libconfig++

zmq 4.1.4

```
git clone https://github.com/zeromq/libzmq
./autogen.sh && ./configure && make -j 4
make check && make install && ldconfig
```

google glog

```
wget https://github.com/google/glog/archive/v0.3.4.tar.gz
tar zxvf v0.3.4.tar.gz
cd glog-0.3.4
./configure && make && make install
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
