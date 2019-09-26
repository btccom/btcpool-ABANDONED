Install Redis
==============

* OS: `Ubuntu 16.04 LTS, 64 Bits`, `Ubuntu 18.04 LTS, 64 Bits`
* Redis: `v3.0.6 or later`
* see more: https://redis.io/download#installation

**install from software source**

```
apt update
apt install -y redis-server
```

**install from source code**

```
mkdir /root/source
cd /root/source
wget http://download.redis.io/releases/redis-4.0.10.tar.gz
tar xzf redis-4.0.10.tar.gz
cd redis-4.0.10
make && make install
cp ./redis.conf /etc/
```

**edit conf**

The default configure file is OK. If you want to add a password of you Redis, refer to the following steps:

`vim /etc/redis/redis.conf`

Search `requirepass` and uncomment it. If the line does not exist, add a new line.

Example:

```
requirepass test123
```

You can change the default port or binding IP by edit this line:

```
port 6379
bind 127.0.0.1
```

**start server**

```
service redis-server start
```

If you install it from source code:
```
redis-server /etc/redis/redis.conf
```

**use supervisor**

`supervisor` is not necessary for redis, but it works.

```
apt-get install supervisor
```

edit conf file `vim /etc/supervisor/conf.d/redis.conf`:

```
[program:redis]
directory=/var/log/redis
command=redis-server /etc/redis/redis.conf --daemonize no 
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
> start/stop redis
```
