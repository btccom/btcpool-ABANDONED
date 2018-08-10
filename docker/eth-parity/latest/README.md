Docker for Parity Ethereum Node
============================

* OS: `Ubuntu 16.04 LTS`
* Docker image: Parity official
* Parity version: latest

## Install Docker CE

```
# Use official mirrors
curl -fsSL https://get.docker.com | bash -s docker

# Or use Aliyun mirrors
curl -fsSL https://get.docker.com | bash -s docker --mirror Aliyun
```

## Change Image Mirrors and Data Root (optional)

```
mkdir -p /work/docker
vim /etc/docker/daemon.json
```

```
{
    "registry-mirrors": ["https://<REPLACED-TO-YOUR-MIRROR-ID>.mirror.aliyuncs.com"],
    "data-root": "/work/docker"
}
```

```
service docker restart
```

## Pull Docker Images

```
docker pull parity/parity:latest
```

## Create Config Files

### Ethereum

```
mkdir -p /work/ethereum/eth-parity
vim /work/ethereum/eth-parity/config.toml
```

```
# Parity Config Generator
# https://paritytech.github.io/parity-config-generator/
#
# This config should be placed in following path:
#   ~/.local/share/io.parity.ethereum/config.toml

[parity]
# Ethereum Main Network
chain = "foundation"
# Parity continously syncs the chain
mode = "last"

[rpc]
#  JSON-RPC will be listening for connections on IP 0.0.0.0.
interface = "0.0.0.0"
# Allows Cross-Origin Requests from domain '*'.
cors = ["*"]

[mining]
# Account address to receive reward when block is mined.
author = "<REPLACED-TO-YOUR-ADDRESS>"
# Blocks that you mine will have this text in extra data field.
extra_data = "/Project BTCPool/"

[network]
# Parity will sync by downloading latest state first. Node will be operational in couple minutes.
warp = true

[misc]
logging = "own_tx,sync=debug"
log_file = "/root/.local/share/io.parity.ethereum/parity.log"
```

### Ethereum Classic

```
mkdir -p /work/ethereum/etc-parity
vim /work/ethereum/etc-parity/config.toml
```

```
# Parity Config Generator
# https://paritytech.github.io/parity-config-generator/
#
# This config should be placed in following path:
#   ~/.local/share/io.parity.ethereum/config.toml

[parity]
# Ethereum Classic Main Network
chain = "classic"
# Parity continously syncs the chain
mode = "last"

[rpc]
#  JSON-RPC will be listening for connections on IP 0.0.0.0.
interface = "0.0.0.0"
# Allows Cross-Origin Requests from domain '*'.
cors = ["*"]

[mining]
# Account address to receive reward when block is mined.
author = "<REPLACED-TO-YOUR-ADDRESS>"
# Blocks that you mine will have this text in extra data field.
extra_data = "/Project BTCPool/"

[network]
# Parity will sync by downloading latest state first. Node will be operational in couple minutes.
warp = true

[misc]
logging = "own_tx=info,sync=info,chain=info,network=info,miner=info"
log_file = "/root/.local/share/io.parity.ethereum/parity.log"
```

## Start Docker Container

### Ethereum

```
# start docker
docker run -it -v /work/ethereum/eth-parity/:/root/.local/share/io.parity.ethereum/ -p 8545:8545 -p 30303:30303 --name eth-parity --restart always -d parity/parity:latest

# see the log
tail -f /work/ethereum/eth-parity/parity.log

# login
docker exec -it eth-parity /bin/bash
```

### Ethereum Classic

```
# start docker
docker run -it -v /work/ethereum/etc-parity/:/root/.local/share/io.parity.ethereum/ -p 8555:8545 -p 30403:30303 --name etc-parity --restart always -d parity/parity:latest

# see the log
tail -f /work/ethereum/etc-parity/parity.log

# login
docker exec -it eth-parity /bin/bash
```
