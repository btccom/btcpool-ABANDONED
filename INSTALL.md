Install BTC.COM Pool
=====================

* OS: `Ubuntu 14.04 LTS, 64 Bits`

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
apt-get install -y openssl libssl-dev libcurl4-openssl-dev libconfig++-dev libboost-all-dev libmysqlclient-dev
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
cd /work/btcpool
bash ../install/init_folders.sh
```

**create kafka topics**

Login to one of kafka machines, than create topics for `btcpool`:

```
cd /work/kafka

#
# "10.0.0.1:2181,10.0.0.2:2181,10.0.0.3:2181" is ZooKeeper cluster.
#
./bin/kafka-topics.sh --create --topic RawGbt      --zookeeper 10.0.0.1:2181,10.0.0.2:2181,10.0.0.3:2181 --replication-factor 2 --partitions 1
./bin/kafka-topics.sh --create --topic StratumJob  --zookeeper 10.0.0.1:2181,10.0.0.2:2181,10.0.0.3:2181 --replication-factor 2 --partitions 1 
./bin/kafka-topics.sh --create --topic SolvedShare --zookeeper 10.0.0.1:2181,10.0.0.2:2181,10.0.0.3:2181 --replication-factor 3 --partitions 1 
./bin/kafka-topics.sh --create --topic ShareLog    --zookeeper 10.0.0.1:2181,10.0.0.2:2181,10.0.0.3:2181 --replication-factor 2 --partitions 1

#
# for namecoin merged mining
#
./bin/kafka-topics.sh --create --topic NMCAuxBlock    --zookeeper 10.0.0.1:2181,10.0.0.2:2181,10.0.0.3:2181 --replication-factor 2 --partitions 1
./bin/kafka-topics.sh --create --topic NMCSolvedShare --zookeeper 10.0.0.1:2181,10.0.0.2:2181,10.0.0.3:2181 --replication-factor 2 --partitions 1
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
```

Use `supervisor` to manager service.

```
apt-get install supervisor
```

