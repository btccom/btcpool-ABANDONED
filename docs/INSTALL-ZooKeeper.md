Install ZooKeeper
=================

* OS: `Ubuntu 14.04 LTS, 64 Bits`, `Ubuntu 16.04 LTS, 64 Bits`
* ZooKeeper: `v3.4.5`

---

**Install ZooKeeper**

```
apt-get install -y zookeeper zookeeper-bin zookeeperd
```

**mkdir for data**

```
mkdir -p /work/zookeeper
mkdir /work/zookeeper/version-2
touch /work/zookeeper/myid
chown -R zookeeper:zookeeper /work/zookeeper
```

**set machine id**

```
# set 1 as myid
echo 1 > /work/zookeeper/myid
```

**edit config file**

`vim /etc/zookeeper/conf/zoo.cfg`, example:

```
# The number of ticks that the initial
# synchronization phase can take
initLimit=5

# The number of ticks that can pass between
# sending a request and getting an acknowledgement
syncLimit=2

# the port at which the clients will connect
clientPort=2181
clientPortAddress=10.0.0.1

# the directory where the snapshot is stored.
dataDir=/work/zookeeper

# specify all zookeeper servers
server.1=10.0.0.1:2888:3888
server.2=10.0.0.2:2888:3888
server.3=10.0.0.3:2888:3888
```

**start/stop service**

```
# after all things done, restart it 
service zookeeper restart

#service zookeeper start/stop/restart/status
```
