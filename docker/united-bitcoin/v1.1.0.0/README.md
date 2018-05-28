Docker for UnitedBitcoin v1.1.0.0
============================

* OS: `Ubuntu 14.04 LTS`, `Ubuntu 16.04 LTS`
* Docker Image OS: `Ubuntu 16.04 LTS`
* UnitedBitcoin: `v1.1.0.0`

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
cd btcpool/docker/united-bitcoin/v1.1.0.0

# If your server is in China, please check "Dockerfile" and uncomment some lines.
# If you want to enable testnet3, please uncomment several lines behind `# service for testnet3`

# build
docker build -t united-bitcoin:1.1.0.0 .
# docker build --no-cache -t united-bitcoin:1.1.0.0 .

# mkdir for united-bitcoin
mkdir -p /work/united-bitcoin

# bitcoin.conf
touch /work/united-bitcoin/bitcoin.conf
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
docker run -it -v /work/united-bitcoin:/root/.bitcoin --name united-bitcoin -p 8333:8333 -p 8332:8332 -p 8331:8331 -p 18333:18333 -p 18332:18332 -p 18331:18331 --restart always -d united-bitcoin:1.1.0.0

# login
docker exec -it united-bitcoin /bin/bash
```
