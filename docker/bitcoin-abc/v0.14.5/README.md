Docker for Bitcoin-abc v0.14.5
============================

* OS: `Ubuntu 14.04 LTS`
* Docker Image OS: `Ubuntu 16.04 LTS`
* Bitcoin-abc: `v0.14.5`

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

git clone https://github.com/btccom/bccpool.git
cd bccpool/docker/bitcoin-abc/v0.14.5

# If your server is in China, please check "Dockerfile" and uncomment some lines.
# If you want to enable testnet, please uncomment several lines behind `# service for testnet`

# build
docker build -t bitcoin-abc:0.14.5 .
# docker build --no-cache -t bitcoin-abc:0.14.5 .

# mkdir for bitcoin-abc
mkdir -p /work/bitcoin-abc

# bitcoin.conf
touch /work/bitcoin-abc/bitcoin.conf
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

# use 8MB block when call GBT
# The blockmaxsize should be between 1000001 and 8000000.
blockmaxsize=8000000
```

## Start Docker Container

```
# start docker
docker run -it -v /work/bitcoin-abc:/root/.bitcoin --name bitcoin-abc -p 8333:8333 -p 8332:8332 -p 8331:8331 --restart always -d bitcoin-abc:0.14.5
#docker run -it -v /work/bitcoin-abc:/root/.bitcoin --name bitcoin-abc -p 8333:8333 -p 8332:8332 -p 8331:8331 -p 18333:18333 -p 18332:18332 -p 18331:18331 --restart always -d bitcoin-abc:0.14.5

# login
docker exec -it bitcoin-abc /bin/bash
```
