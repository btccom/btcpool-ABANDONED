Docker for SuperSmartBitcoin v0.16.2
============================

* OS: `Ubuntu 14.04 LTS`, `Ubuntu 16.04 LTS`
* Docker Image OS: `Ubuntu 16.04 LTS`
* SuperSmartBitcoin: `v0.16.2`

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
cd btcpool/docker/super-bitcoin/v0.16.2

# If your server is in China, please check "Dockerfile" and uncomment some lines.
# If you want to enable testnet3, please uncomment several lines behind `# service for testnet3`

# build
docker build -t super-bitcoin:0.16.2 .
# docker build --no-cache -t super-bitcoin:0.16.2 .

# mkdir for super-bitcoin
mkdir -p /work/super-bitcoin

# bitcoin.conf
ln -s ./bitcoin.conf /work/super-bitcoin/sbtc.conf
touch /work/super-bitcoin/bitcoin.conf
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

# use 1G memory for utxo, depends on your machine's memory
dbcache=1000
```

## Start Docker Container

```
# start docker
docker run -it -v /work/super-bitcoin:/root/.bitcoin --name super-bitcoin -p 8333:8333 -p 8332:8332 -p 8331:8331 -p 18333:18333 -p 18332:18332 -p 18331:18331 --restart always -d super-bitcoin:0.16.2

# login
docker exec -it super-bitcoin /bin/bash
```
