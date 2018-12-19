Docker for Litecoind v0.16.3
============================

* Docker Image OS: `Ubuntu 16.04 LTS`
* litecoind: `v0.16.3`

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
cd btcpool/docker/litecoind/v0.16.3

# If your server is in China, please check "Dockerfile" and uncomment some lines.
# If you want to enable testnet3, please uncomment several lines behind `# service for testnet3`

# build
docker build -t litecoind:0.16.3 .
# docker build --no-cache -t litecoind:0.16.3 .

# mkdir for litecoind
mkdir -p /work/litecoind

# litecoin.conf
touch /work/litecoind/litecoin.conf
```

### litecoin.conf example

```
rpcuser=litecoinrpc
# generate random rpc password:
#   $ strings /dev/urandom | grep -o '[[:alnum:]]' | head -n 30 | tr -d '\n'; echo
rpcpassword=xxxxxxxxxxxxxxxxxxxxxxxxxx
rpcthreads=4

rpcallowip=172.16.0.0/12
rpcallowip=192.168.0.0/16
rpcallowip=10.0.0.0/8

# use 1G memory for utxo, depends on your machine's memory
dbcache=1000

# set maximum BIP141 block weight
blockmaxweight=4000000
```

## Start Docker Container

```
# start docker
docker run -it -v /work/litecoind:/root/.litecoin --name litecoind -p 9331-9333:9331-9333 --restart always -d litecoind:0.16.3

# login
docker exec -it litecoind /bin/bash
litecoin-cli getblockchaininfo
```