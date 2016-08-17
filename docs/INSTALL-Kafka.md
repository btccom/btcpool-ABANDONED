Install Kafka
==============

* OS: `Ubuntu 14.04 LTS, 64 Bits`
* Kafka: `v0.10.0.0`
* see more: https://kafka.apache.org/documentation.html#quickstart

**install depends**

```
apt-get install -y default-jre
```

**install Kafka**

```
mkdir /root/source
cd /root/source
wget http://ftp.cuhk.edu.hk/pub/packages/apache.org/kafka/0.10.0.0/kafka_2.11-0.10.0.0.tgz
 
mkdir /work/kafka
cd /work/kafka
tar -zxf /root/source/kafka_2.11-0.10.0.0.tgz --strip 1
```

**edit conf**

`vim /work/kafka/config/server.properties`

The broker's id is `1`.

```
# The id of the broker. This must be set to a unique integer for each broker.
broker.id=1

log.dirs=/work/kafka-logs
listeners=PLAINTEXT://10.0.0.4:9092

zookeeper.connect=10.0.0.1:2181,10.0.0.2:2181,10.0.0.3:2181
```

**start server**

```
cd /work/kafka
./bin/kafka-server-start.sh /work/kafka/config/server.properties > /work/kafka/kafka.log 2>&1
```