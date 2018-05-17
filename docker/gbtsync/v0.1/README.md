Docker for GBT Synchronizer of Bitcoin-ABC-v0.17.1-lightgbt
============================

* OS: `Ubuntu 14.04 LTS`, `Ubuntu 16.04 LTS`
* Docker Image OS: `Ubuntu 16.04 LTS`

See also: [README of gbtsync](../../src/gbtsync/README.md)

## Install Docker

```
# Use 'curl -sSL https://get.daocloud.io/docker | sh' instead of this line
# when your server is in China.
wget -qO- https://get.docker.com/ | sh

service docker start
service docker status
```

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
touch /work/bitcoin-abc/gbtsync.cfg
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
        server = "localhost";
        username = "root";
        password = "password";
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
docker run -it -v /work/bitcoin-abc:/datadir --name gbtsync --restart always -d bitcoin-abc:0.17.1-lightgbt

# login
docker exec -it gbtsync /bin/bash
```
