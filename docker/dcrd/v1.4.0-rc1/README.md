Docker for Dcrd v1.4.0-rc1
============================

* OS: `Ubuntu 14.04 LTS`
* Docker Image OS: `Ubuntu 16.04 LTS`
* Dcrd: `v1.4.0-rc1`

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
cd btcpool/docker/dcrd/v1.4.0-rc1

# If your server is in China, please check "Dockerfile" and uncomment some lines.

# build
docker build -t dcrd:1.4.0-rc1 .
# docker build --no-cache -t dcrd:1.4.0-rc1 .

# mkdir for dcrd
mkdir -p /work/dcrd

# dcrd.conf
touch /work/dcrd/dcrd.conf
```

### dcrd.conf example

```
miningaddr=xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

rpcuser=decredrpc

# generate random rpc password:
#   $ strings /dev/urandom | grep -o '[[:alnum:]]' | head -n 30 | tr -d '\n'; echo
rpcpassword=xxxxxxxxxxxxxxxxxxxxxxxxxx

# omit address part to listen on any available addresss
rpclisten=address:port

# omit address part to listen on any available addresss
listen=address:port

# uncomment to enable testnet
# testnet=1
```

## Start Docker Container

```
# start docker (mainnet, assuming listening port 9108 & RPC listening port 9109)
docker run -it -v /work/dcrd:/root/.dcrd --name dcrd -p 9108:9108 -p 9109:9109 --restart always -d dcrd:1.4.0-rc1

# login
docker exec -it dcrd /bin/bash
```
