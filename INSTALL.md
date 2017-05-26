Install BTC.COM Pool
=====================

* OS: `Ubuntu 14.04 LTS, 64 Bits`
* OS: `Ubuntu 16.04 LTS, 64 Bits`

## Build

If you are the first time build btcpool, you could run `bash install/install_btcpool.sh` instead of exec these shell commands one by one.

```
cd /work
wget https://raw.githubusercontent.com/btccom/btcpool/master/install/install_btcpool.sh
bash ./install_btcpool.sh
```

### Dependency

```
apt-get update

apt-get install -y build-essential autotools-dev libtool autoconf automake pkg-config cmake
apt-get install -y openssl libssl-dev libcurl4-openssl-dev libconfig++-dev libboost-all-dev libgmp-dev libmysqlclient-dev libzookeeper-mt-dev
```

* zmq-v4.1.5

```
mkdir -p /root/source && cd /root/source
wget https://github.com/zeromq/zeromq4-1/releases/download/v4.1.5/zeromq-4.1.5.tar.gz
tar zxvf zeromq-4.1.5.tar.gz
cd zeromq-4.1.5
./autogen.sh && ./configure && make
make check && make install && ldconfig
```

* glog-v0.3.4

```
mkdir -p /root/source && cd /root/source
wget https://github.com/google/glog/archive/v0.3.4.tar.gz
tar zxvf v0.3.4.tar.gz
cd glog-0.3.4
./configure && make && make install
```

* librdkafka-v0.9.1

```
apt-get install -y zlib1g zlib1g-dev
mkdir -p /root/source && cd /root/source
wget https://github.com/edenhill/librdkafka/archive/0.9.1.tar.gz
tar zxvf 0.9.1.tar.gz
cd librdkafka-0.9.1
./configure && make && make install
```

* libevent

```
mkdir -p /root/source && cd /root/source
wget https://github.com/libevent/libevent/releases/download/release-2.0.22-stable/libevent-2.0.22-stable.tar.gz
tar zxvf libevent-2.0.22-stable.tar.gz
cd libevent-2.0.22-stable
./configure
make
make install
```

### btcpool

```
mkdir -p /work && cd /work
git clone https://github.com/btccom/btcpool.git
cd /work/btcpool
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

---

## Init btcpool

Now create folder for btcpool, if you are going to run all service in one machine you could run `install/init_folders.sh` as below.

```
cd /work/btcpool/build
bash ../install/init_folders.sh
```

**create kafka topics**

Login to one of kafka machines, than create topics for `btcpool`:

```
cd /work/kafka

#
# "10.0.0.1:2181,10.0.0.2:2181,10.0.0.3:2181" is ZooKeeper cluster.
#
./bin/kafka-topics.sh --create --topic RawGbt         --zookeeper 10.0.0.1:2181,10.0.0.2:2181,10.0.0.3:2181 --replication-factor 2 --partitions 1
./bin/kafka-topics.sh --create --topic StratumJob     --zookeeper 10.0.0.1:2181,10.0.0.2:2181,10.0.0.3:2181 --replication-factor 2 --partitions 1 
./bin/kafka-topics.sh --create --topic SolvedShare    --zookeeper 10.0.0.1:2181,10.0.0.2:2181,10.0.0.3:2181 --replication-factor 3 --partitions 1 
./bin/kafka-topics.sh --create --topic ShareLog       --zookeeper 10.0.0.1:2181,10.0.0.2:2181,10.0.0.3:2181 --replication-factor 2 --partitions 1
./bin/kafka-topics.sh --create --topic NMCAuxBlock    --zookeeper 10.0.0.1:2181,10.0.0.2:2181,10.0.0.3:2181 --replication-factor 2 --partitions 1
./bin/kafka-topics.sh --create --topic NMCSolvedShare --zookeeper 10.0.0.1:2181,10.0.0.2:2181,10.0.0.3:2181 --replication-factor 2 --partitions 1
./bin/kafka-topics.sh --create --topic CommonEvents   --zookeeper 10.0.0.1:2181,10.0.0.2:2181,10.0.0.3:2181 --replication-factor 2 --partitions 1

# do not keep 'RawGbt' message more than 12 hours
./bin/kafka-topics.sh --zookeeper 10.0.0.1:2181,10.0.0.2:2181,10.0.0.3:2181 --alter --topic RawGbt       --config retention.ms=43200000
# 'CommonEvents': 24 hours
./bin/kafka-topics.sh --zookeeper 10.0.0.1:2181,10.0.0.2:2181,10.0.0.3:2181 --alter --topic CommonEvents --config retention.ms=86400000
```

Check kafka topics stutus:

```
# show topics
$ ./bin/kafka-topics.sh --describe --zookeeper 10.0.0.1:2181,10.0.0.2:2181,10.0.0.3:2181

# if create all topics success, will output:
Topic:RawGbt   	PartitionCount:1       	ReplicationFactor:2    	Configs:
       	Topic: RawGbt  	Partition: 0   	Leader: 1      	Replicas: 1,2  	Isr: 1,2
Topic:ShareLog 	PartitionCount:1       	ReplicationFactor:2    	Configs:
       	Topic: ShareLog	Partition: 0   	Leader: 2      	Replicas: 2,1  	Isr: 2,1
Topic:SolvedShare      	PartitionCount:1       	ReplicationFactor:3    	Configs:
       	Topic: SolvedShare     	Partition: 0   	Leader: 1      	Replicas: 1,2,3	Isr: 1,2,3
Topic:StratumJob       	PartitionCount:1       	ReplicationFactor:2    	Configs:
       	Topic: StratumJob      	Partition: 0   	Leader: 1      	Replicas: 1,2  	Isr: 1,2
Topic:NMCAuxBlock        PartitionCount:1         ReplicationFactor:3      Configs:
         Topic: NMCAuxBlock       Partition: 0     Leader: 3        Replicas: 3,1  Isr: 3,1
Topic:NMCSolvedShare     PartitionCount:1         ReplicationFactor:3      Configs:
         Topic: NMCSolvedShare    Partition: 0     Leader: 3        Replicas: 3,1  Isr: 3,1
```

Before you start btcpool's services, you need to stetup MySQL database and bitcoind/namecoind (if enable merged mining).

Init MySQL database tables.

```
#
# create database `bpool_local_db` and `bpool_local_stats_db`
#

#
# than create tables from sql
#
# bpool_local_db
mysql -uxxxx -p -h xxx.xxx.xxx bpool_local_db       < install/bpool_local_db.sql

# bpool_local_stats_db
mysql -uxxxx -p -h xxx.xxx.xxx bpool_local_stats_db < install/bpool_local_stats_db.sql

```

**User list API** 

You need to implement a HTTP API for stratum server. The API is use to fetch user list in incremental model. Stratum Server will call this API every few seconds, interval is about 10 seconds. When Stratum Server start, it will fetch again and again util no more users.

Should works as below.

```
#
# `last_id` is the offset. pagesize is 5. total user count is 8.
#
$ curl http://xxx.xxx.xxx/GetUserList?last_id=0
{"err_no":0,"err_msg":null,"data":{"litianzhao":1,"spica":2,"fazhu":3,"kevin":4,"kevin1":5}}

# fetch next page
$ curl http://xxx.xxx.xxx/GetUserList?last_id=5
{"err_no":0,"err_msg":null,"data":{"kevin2":6,"kevin3":7,"Miles":8}}

# fetch next page, no more users
$ curl http://xxx.xxx.xxx/GetUserList?last_id=8
{"err_no":0,"err_msg":null,"data":{}}
```

Use `supervisor` to manager services.


```
apt-get install supervisor

#
# change open file limits, vim /etc/supervisor/supervisord.conf
# in the [supervisord] section, add 'minfds=65535'.
# $ service supervisor restart
#
cd /work/btcpool/build


################################################################################
# for common server
################################################################################
cp ../install/supervisord/gbtmaker.conf      /etc/supervisor/conf.d
cp ../install/supervisord/jobmaker.conf      /etc/supervisor/conf.d
cp ../install/supervisord/blkmaker.conf      /etc/supervisor/conf.d
cp ../install/supervisord/sharelogger.conf   /etc/supervisor/conf.d
#
# if you need to enable watch other pool's stratum job. poolwatcher will 
# generate empty getblocktemplate when they find a new height stratum job 
# from other pools. this could reduce pool's orphan block rate.
#
cp ../install/supervisord/poolwatcher.conf   /etc/supervisor/conf.d
#
# enable namecoin merged mining
#
cp ../install/supervisord/nmcauxmaker.conf   /etc/supervisor/conf.d

#
# start services: gbtmaker, jobmaker, blkmaker, sharelogger...
#
$ supervisorctl
> reread
> update
> status


#
# start your stratum server(see below) and add some miners to the pool, after make
# some shares than start 'slparser' & 'statshttpd'
#
cp ../install/supervisord/slparser.conf      /etc/supervisor/conf.d
cp ../install/supervisord/statshttpd.conf    /etc/supervisor/conf.d

# start services: slparser, statshttpd
$ supervisorctl
> reread
> update
> status


################################################################################
# for stratum server
################################################################################
cp ../install/supervisord/sserver.conf       /etc/supervisor/conf.d

$ supervisorctl
> reread
> update
> status
```

## Upgrade btcpool

Check release note and upgrade guide. Maybe add some config options to config file or change something of database table. 

Get the latest codes and rebuild:

```
cd /work/btcpool/build
git pull
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

use `supervisorctl` to restart your services:

```
$ supervisorctl
> restart xxxx
```
