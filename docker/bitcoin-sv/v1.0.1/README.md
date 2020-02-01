Docker for Bitcoin SV v1.0.1
============================

* OS: `Ubuntu 14.04 LTS`, `Ubuntu 16.04 LTS`
* Docker Image OS: `Ubuntu 16.04 LTS`
* Bitcoin SV: `v1.0.1`

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

git clone git@github.com:btccom/btcpool.git
cd btcpool/docker/bitcoin-sv/v1.0.1

# If your server is in China, please check "Dockerfile" and uncomment some lines.
# If you want to enable testnet, please uncomment several lines behind `# service for testnet`

# build
docker build -t bitcoin-sv:1.0.1 .
# docker build --no-cache -t bitcoin-sv:1.0.1 .

# mkdir for bitcoin-sv
mkdir -p /work/bitcoin-sv

# bitcoin.conf
touch /work/bitcoin-sv/bitcoin.conf
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

excessiveblocksize=1000000000
maxstackmemoryusageconsensus=100000000
```

## Start Docker Container

```
# start docker
docker run -it -v /work/bitcoin-sv:/root/.bitcoin --name bitcoin-sv -p 8333:8333 -p 8332:8332 -p 8331:8331 --restart always -d bitcoin-sv:1.0.1
#docker run -it -v /work/bitcoin-sv:/root/.bitcoin --name bitcoin-sv -p 8333:8333 -p 8332:8332 -p 8331:8331 -p 18333:18333 -p 18332:18332 -p 18331:18331 --restart always -d bitcoin-sv:1.0.1

# login
docker exec -it bitcoin-sv /bin/bash
```
