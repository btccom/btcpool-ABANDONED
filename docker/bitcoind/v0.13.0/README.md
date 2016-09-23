Docker for Bitcoind v0.13.0
============================

* OS: `Ubuntu 14.04 LTS`
* Docker Image OS: `Ubuntu 16.04 LTS`
* Bitcoind: `v0.13.0`

**WARNING: Miners should not change blockmaxsize to blockmaxweight at this time. (v0.13.0)**

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
cd btcpool/docker/bitcoind/v0.13.0

# If your server is in China, please check "Dockerfile" and uncomment some lines

# build
docker build -t bitcoind:0.13.0 .
# docker build --no-cache -t bitcoind:0.13.0 .

# mkdir for bitcoind
mkdir -p /work/bitcoind

# bitcoin.conf
touch /work/bitcoind/bitcoin.conf
```

### bitcoin.conf example

```
rpcuser=bitcoinrpc
# generate random rpc password:
#   $ strings /dev/urandom | grep -o '[[:alnum:]]' | head -n 30 | tr -d '\n'; echo
rpcpassword=xxxxxxxxxxxxxxxxxxxxxxxxxx
rpcthreads=4

rpcallowip=172.16.0.0/12
rpcallowip=192.168.0.0/16
rpcallowip=10.0.0.0/8

# we use ZMQ to get notification when new block is coming
zmqpubrawblock=tcp://0.0.0.0:8331
zmqpubrawtx=tcp://0.0.0.0:8331
zmqpubhashtx=tcp://0.0.0.0:8331
zmqpubhashblock=tcp://0.0.0.0:8331

# use 2G memory for utxo, depends on your machine's memory
dbcache=2000

# use 1MB block when call GBT
# Miners should not change blockmaxsize to blockmaxweight at this time.
blockmaxsize=1000000
```

## Start Docker Container

```
# start docker
docker run -it -v /work/bitcoind:/root/.bitcoin --name bitcoind -p 8333:8333 -p 8332:8332 -p 8331:8331 --restart always -d bitcoind:0.13.0

# login
docker exec -it bitcoind /bin/bash
```
