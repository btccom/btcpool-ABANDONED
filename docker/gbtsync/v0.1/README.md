Docker for GBT Synchronizer of Bitcoin-ABC-v0.17.1-lightgbt
============================

* OS: `Ubuntu 14.04 LTS`, `Ubuntu 16.04 LTS`
* Docker Image OS: `Ubuntu 16.04 LTS`

See also: [README of gbtsync](../../../src/gbtsync/README.md)

## Install Docker

```
# Use 'curl -sSL https://get.daocloud.io/docker | sh' instead of this line
# when your server is in China.
wget -qO- https://get.docker.com/ | sh

service docker start
service docker status
```

## Install MySQL

Each server group that needs to be synchronized shares one MySQL.

### create config file
```
mkdir -p /work/db-gbtsync/etc
mkdir -p /work/db-gbtsync/data
vim /work/db-gbtsync/etc/my.cnf
```

### my.cnf example
```
[mysqld]
skip-host-cache
skip-name-resolve
max_allowed_packet=64M
```

### install & run mysql
```
docker pull mysql
docker run -e MYSQL_ROOT_PASSWORD=<your-password> -p 3306:3306 -v /work/db-gbtsync/etc:/etc/mysql/conf.d/ -v /work/db-gbtsync/data:/var/lib/mysql --name db-gbtsync -d mysql
```

### import tables
```
docker exec -it db-gbtsync mysql -uroot -p
```
```sql
create database gbtsync;
use gbtsync;
```
Then executing the [create table sql](../../../src/gbtsync/create_table.sql).

## Build Docker Images

```
cd /work

git clone https://github.com/btccom/btcpool.git
cd btcpool/docker/gbtsync/v0.1

# If your server is in China, please check "Dockerfile" and uncomment some lines.

# build
docker build -t gbtsync:v0.1 .
# docker build --no-cache -t gbtsync:v0.1 .


# gbtsync.cfg
vim /work/bitcoin-abc/gbtsync.cfg
```

### gbtsync.cfg example

```
workers =
(
    {
        type = "file";
        path = "/datadir/gbt/";
        prefix = "GBT";
        postfix = "GBT";
        name = "bch01";
        time = 5000;
    },
    {
        type = "mysql";
        server = "192.168.34.56";
        port = 3306;
        username = "root";
        password = "<your-password>";
        dbschema = "gbtsync";
        tablename = "filedata";
        name = "manager01";
        time = 1000;
        # syncdelete = false;   #  to ignore deleted data/file from other workers 
    }
)
```

## Start Docker Container

```
# start docker
docker run -it -v /work/bitcoin-abc:/datadir --name gbtsync --restart always -d gbtsync:v0.1

# login
docker exec -it gbtsync /bin/bash
```
