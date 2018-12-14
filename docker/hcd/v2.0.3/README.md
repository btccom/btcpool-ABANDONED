Docker for Hcd v2.0.3
============================

* OS: `Ubuntu 14.04 LTS`
* Docker Image OS: `Ubuntu 18.04 LTS`
* hcd: `v2.0.3`

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
cd btcpool/docker/hcd/v2.0.3

# If your server is in China, please check "Dockerfile" and uncomment some lines.

# build
docker build -t hcd:2.0.3 .
# docker build --no-cache -t hcd:2.0.3 .

# mkdir for hcd
mkdir -p /work/hcd

# hcd.conf
touch /work/hcd/hcd.conf
```

### hcd.conf example

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
docker run -it -v /work/hcd:/root/.hcd --name hcd -p 9108:9108 -p 9109:9109 --restart always -d hcd:2.0.3

# login
docker exec -it hcd /bin/bash
```

Once the container is started, RPC user needs to have `rpc.cert` under the data directory for TLS connection.
