Docker for Parity Ethereum Node
============================

* OS: `Ubuntu 16.04 LTS`
* Docker image: Parity official
* Parity version: stable

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
docker pull parity/parity:stable
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
# Parity will sync by downloading stable state first. Node will be operational in couple minutes.
warp = true

[misc]
logging = "own_tx,sync=debug"
log_file = "/home/parity/.local/share/io.parity.ethereum/parity.log"
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
# Parity will sync by downloading stable state first. Node will be operational in couple minutes.
warp = true

[misc]
logging = "own_tx=info,sync=info,chain=info,network=info,miner=info"
log_file = "/home/parity/.local/share/io.parity.ethereum/parity.log"
```

## Start Docker Container

### Ethereum

```
# start docker
docker run -it -v /work/ethereum/eth-parity/:/home/parity/.local/share/io.parity.ethereum/ -p 8545:8545 -p 30303:30303 --name eth-parity --restart always -d parity/parity:stable

# see the log
tail -f /work/ethereum/eth-parity/parity.log

# login
docker exec -it eth-parity /bin/bash
```

### Ethereum Classic

```
# start docker
docker run -it -v /work/ethereum/etc-parity/:/home/parity/.local/share/io.parity.ethereum/ -p 8555:8545 -p 30403:30303 --name etc-parity --restart always -d parity/parity:stable

# see the log
tail -f /work/ethereum/etc-parity/parity.log

# login
docker exec -it eth-parity /bin/bash
```

## How to upgrade from old deploment

The old version of the parity container running with the user `root`,
but now it running with the user `parity`.

So before upgrading to the new version, you need to do some extra works, otherwise Parity will not able to start.

The following command shows what you need to do:

### For Ethereum

```
# Stop and remove the old container
docker stop eth-parity
docker rm eth-parity

# The new user name is parity, its uid = 1000 and gid = 1000
chown -R 1000:1000 /work/ethereum/eth-parity

# Paths in config file need to update
sed -i 's@/root/@/home/parity/@g' /work/ethereum/eth-parity/config.toml

# Now you can run the new container
docker run -it -v /work/ethereum/eth-parity/:/home/parity/.local/share/io.parity.ethereum/ -p 8545:8545 -p 30303:30303 --name eth-parity --restart always -d parity/parity:stable

# Check if Parity is running properly
tail -F /work/ethereum/eth-parity/parity.log
```

### For Ethereum Classic

```
# Stop and remove the old container
docker stop etc-parity
docker rm etc-parity

# The new user name is parity, its uid = 1000 and gid = 1000
chown -R 1000:1000 /work/ethereum/etc-parity

# Paths in config file need to update
sed -i 's@/root/@/home/parity/@g' /work/ethereum/etc-parity/config.toml

# Now you can run the new container
docker run -it -v /work/ethereum/etc-parity/:/home/parity/.local/share/io.parity.ethereum/ -p 8545:8545 -p 30303:30303 --name etc-parity --restart always -d parity/parity:stable

# Check if Parity is running properly
tail -F /work/ethereum/etc-parity/parity.log
```
