Docker for beam-node (patched for mining)
============================

* Docker Image OS: `Ubuntu 18.04 LTS`
* beam-node: `main-release-mining`

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
cd btcpool/docker/beam-node/main-release-mining

# If your server is not in China, you may want edit "Dockerfile" and comment `ADD sources-aliyun.com.list`.

# build
docker build -t beam-node:main-release-mining .
# docker build --no-cache -t beam-node:main-release-mining .

# mkdir for beam-node
mkdir -p /work/beam-node

# create TLS certificate for Stratum protocol
cd /work/beam-node
openssl genrsa -out stratum.key 2048
openssl req -new -key stratum.key -out stratum.crt.req
openssl x509 -req -days 365 -in stratum.crt.req -signkey stratum.key -out stratum.crt

# beam-node.cfg
vim /work/beam-node/beam-node.cfg
```

### beam-node.cfg example

```
# port to start server on
port=8100

# log level [info|debug|verbose]
log_level=info

# file log level [info|debug|verbose]
file_log_level=info

# nodes to connect to
# WARNING: Use domain name in peer list is unsafe because of this issue:
#          <https://github.com/BeamMW/beam/issues/288>
#          Please use IP insteads.
peer = 52.220.54.222:8100
peer = 54.151.57.34:8100
peer = 18.197.8.221:8100

# port to start stratum server on
stratum_port=3333

# path to stratum server api keys file, and tls certificate and private key
stratum_secrets_path=.

# Owner viewer key
owner_key=<please-fill-it>

# Standalone miner key
miner_key=<please-fill-it>

# password for keys
pass=<please-fill-it>
```
### got `owner_key` and `miner_key`
Use [beam-wallet-cli](https://github.com/BeamMW/beam/releases) with these commands:
```
./beam-wallet export_owner_key
./beam-wallet export_miner_key --subkey=1
```

Or see [this document](https://beam-docs.readthedocs.io/en/latest/rtd_pages/user_mining_beam.html#mining-using-external-miner) for details.

Notice: the `pass` is the password you entered in `export_miner_key`.

## Start Docker Container

```
# start docker
docker run -it -v /work/beam-node:/work/beam-node --name beam-node -p 8100:8100 -p 3333:3333 --restart always -d beam-node:main-release-mining

# login
docker exec -it beam-node /bin/bash
```

## Test stratum port (like `telnet localhost 3333`)

The Stratum port of beam-node forces TLS, so you cannot connect to it using telnet. But the openssl command provides a `telnet over TLS` feature:
```bash
openssl s_client -connect localhost:3333
```

Then input this line with enter:
```json
{"method":"login","api_key":"anything","id":"login","jsonrpc":"2.0"}
```

If anything is OK, you will receive a stratum job as a response. It looks like:
```json
{"difficulty":440836216,"height":6656,"id":"487","input":"6a35dae05f5d6b16e419e31ede77c94bf1cec7a6a822636cea10f9c8897914d5","jsonrpc":"2.0","method":"job"}
```

## ACL of Stratum port

You can create a `stratum.api.keys` file contains a list of allowed api_keys.

An API key should contain any number of strings at least 7 symbols long without spaces.
```
vim /work/beam-node/stratum.api.keys
```
```
12345678
sfdskjhfdksk
984398349834
```

If there is no stratum.api.keys, ACL will be switched off.

Please note that stratum.api.key file is reloaded by `beam-node` every 5 seconds, if you want to add/delete/edit key you shouldn't restart beam-node, just edit `stratum.api.keys` file.
