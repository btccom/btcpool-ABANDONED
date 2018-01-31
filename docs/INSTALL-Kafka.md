Install Kafka
==============

* OS: `Ubuntu 14.04 LTS, 64 Bits`, `Ubuntu 16.04 LTS, 64 Bits`
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
wget https://mirrors.tuna.tsinghua.edu.cn/apache/kafka/0.11.0.2/kafka_2.11-0.11.0.2.tgz
 
mkdir -p /work/kafka
cd /work/kafka
tar -zxf /root/source/kafka_2.11-0.11.0.2.tgz --strip 1
```

**edit conf**

`vim /work/kafka/config/server.properties`

The broker's id is `1`.

For tester: if you have only one kafka, set `offsets.topic.replication.factor=1`.

If the broker number less than `offsets.topic.replication.factor`, high-level kafka consumer may not work properly, which means that `sharelogger` cannot obtain any share from kafka.

```
# The id of the broker. This must be set to a unique integer for each broker.
broker.id=1

# The replication factor for the offsets topic.
offsets.topic.replication.factor=3

# increate message size limit
message.max.bytes=60000000
replica.fetch.max.bytes=80000000

log.dirs=/work/kafka-logs
listeners=PLAINTEXT://10.0.0.4:9092

zookeeper.connect=10.0.0.1:2181,10.0.0.2:2181,10.0.0.3:2181
```

**start server**

```
cd /work/kafka
# you could put this line into /etc/rc.local
nohup /work/kafka/bin/kafka-server-start.sh /work/kafka/config/server.properties > /dev/null 2>&1 &

# stop server
# ./bin/kafka-server-stop.sh /work/kafka/config/server.properties
```

**use supervisor**

Recommend to use `supervisor` to manage Kafka service.

```
apt-get install supervisor
```

edit conf file `vim /etc/supervisor/conf.d/kafka.conf`:

```
[program:kafka]
environment=KAFKA_HEAP_OPTS="-Xmx2G -Xms1G"
directory=/work/kafka
command=/work/kafka/bin/kafka-server-start.sh /work/kafka/config/server.properties
autostart=true
autorestart=true
startsecs=6
startretries=20
```

```
$ supervisorctl
> reread
> update
> status

or 
> start/stop kafka
```
