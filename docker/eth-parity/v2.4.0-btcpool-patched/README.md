Docker for Parity Ethereum Node
============================

* OS: `Ubuntu 16.04 LTS`
* Docker image: build by yourself
* Parity version: 2.4.0 with patched `eth_getWork` and `eth_submitWork`

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

## Build Docker Image

```
git clone --depth=1 -b v2.4.0-btcpool https://github.com/btccom/parity-ethereum.git
cd parity-ethereum
docker build -t parity:2.4.0-btcpool -f scripts/docker/alpine/Dockerfile .
```

## Create Config Files

Tips:
* Set a different `<node-id>` for each node to ensure results of getwork are unique.

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
# Ethereum Main Network (ETH)
chain = "foundation"
# Parity continously syncs the chain
mode = "active"
# Disables auto downloading of new releases. Not recommended.
no_download = true

[ipc]
# You won't be able to use IPC to interact with Parity.
disable = true

[dapps]
# You won't be able to access any web Dapps.
disable = true

[rpc]
#  JSON-RPC will be listening for connections on IP 0.0.0.0.
interface = "0.0.0.0"
# Allows Cross-Origin Requests from domain '*'.
cors = ["*"]

[mining]
# Account address to receive reward when block is mined.
author = "<REPLACED-TO-YOUR-ADDRESS>"
# Blocks that you mine will have this text in extra data field.
# Set a different node-id for each node to ensure results of getwork are unique.
extra_data = "<node-id>/Project BTCPool/"
# Prepare a block to seal even when there are no miners connected.
force_sealing = true
# Force the node to author new blocks when a new uncle block is imported.
reseal_on_uncle = true
# New pending block will be created for all transactions (both local and external).
reseal_on_txs = "all"
# New pending block will be created only once per 4000 milliseconds.
reseal_min_period = 4000
# Parity will keep/relay at most 8192 transactions in queue.
tx_queue_size = 8192
tx_queue_per_sender = 128

[network]
# Parity will sync by downloading latest state first. Node will be operational in couple minutes.
warp = true
# Specify a path to a file with peers' enodes to be always connected to.
reserved_peers = "/home/parity/.local/share/io.parity.ethereum/peer.list"
# Parity will try to maintain connection to at least 50 peers.
min_peers = 100
# Parity will maintain at most 200 peers.
max_peers = 200

[misc]
logging = "own_tx=trace,sync=info,chain=info,network=info,miner=trace"
log_file = "/home/parity/.local/share/io.parity.ethereum/parity.log"

[footprint]
# Prune old state data. Maintains journal overlay - fast but extra 50MB of memory used.
pruning = "fast"
# If defined will never use more then 1024MB for all caches. (Overrides other cache settings).
cache_size = 1024

[snapshots]
# Disables automatic periodic snapshots.
disable_periodic = true
```

```
vim /work/ethereum/eth-parity/peer.list
```

```
enode://f4642fa65af50cfdea8fa7414a5def7bb7991478b768e296f5e4a54e8b995de102e0ceae2e826f293c481b5325f89be6d207b003382e18a8ecba66fbaf6416c0@33.4.2.1:30303
enode://pubkey@ip:port
...
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
# Ethereum Classic Network (ETC)
chain = "classic"
# Parity continously syncs the chain
mode = "active"
# Disables auto downloading of new releases. Not recommended.
no_download = true

[ipc]
# You won't be able to use IPC to interact with Parity.
disable = true

[dapps]
# You won't be able to access any web Dapps.
disable = true

[rpc]
#  JSON-RPC will be listening for connections on IP 0.0.0.0.
interface = "0.0.0.0"
# Allows Cross-Origin Requests from domain '*'.
cors = ["*"]

[mining]
# Account address to receive reward when block is mined.
author = "<REPLACED-TO-YOUR-ADDRESS>"
# Blocks that you mine will have this text in extra data field.
# Set a different node-id for each node to ensure results of getwork are unique.
extra_data = "<node-id>/Project BTCPool/"
# Prepare a block to seal even when there are no miners connected.
force_sealing = true
# Force the node to author new blocks when a new uncle block is imported.
reseal_on_uncle = true
# New pending block will be created for all transactions (both local and external).
reseal_on_txs = "all"
# New pending block will be created only once per 4000 milliseconds.
reseal_min_period = 4000
# Parity will keep/relay at most 8192 transactions in queue.
tx_queue_size = 8192
tx_queue_per_sender = 128

[network]
# Parity will sync by downloading latest state first. Node will be operational in couple minutes.
warp = true
# Specify a path to a file with peers' enodes to be always connected to.
reserved_peers = "/home/parity/.local/share/io.parity.ethereum/peer.list"
# Parity will try to maintain connection to at least 50 peers.
min_peers = 100
# Parity will maintain at most 200 peers.
max_peers = 200

[misc]
logging = "own_tx=trace,sync=info,chain=info,network=info,miner=trace"
log_file = "/home/parity/.local/share/io.parity.ethereum/parity.log"

[footprint]
# Prune old state data. Maintains journal overlay - fast but extra 50MB of memory used.
pruning = "fast"
# If defined will never use more then 1024MB for all caches. (Overrides other cache settings).
cache_size = 1024

[snapshots]
# Disables automatic periodic snapshots.
disable_periodic = true
```

```
vim /work/ethereum/etc-parity/peer.list
```

```
enode://0bd387b63251bc3f610df1da4bc4377af8b6214b46ea067859090a9c34de0c6ec8e03627af80599d060bad07d8ead01df642620bdfd611c2e42b1da878aaaad0@33.4.2.1:30303
enode://pubkey@ip:port
...
```

## Start Docker Container

### Ethereum

```
# start docker
docker run -it -v /work/ethereum/eth-parity/:/home/parity/.local/share/io.parity.ethereum/ -p 8545:8545 -p 30303:30303 --name eth-parity --restart always -d parity:2.4.0-btcpool

# see the log
tail -f /work/ethereum/eth-parity/parity.log

# login
docker exec -it eth-parity /bin/bash
```

### Ethereum Classic

```
# start docker
docker run -it -v /work/ethereum/etc-parity/:/home/parity/.local/share/io.parity.ethereum/ -p 8555:8545 -p 30403:30303 --name etc-parity --restart always -d parity:2.4.0-btcpool

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
docker run -it -v /work/ethereum/eth-parity/:/home/parity/.local/share/io.parity.ethereum/ -p 8545:8545 -p 30303:30303 --name eth-parity --restart always -d parity:2.4.0-btcpool

# Check if Parity is running properly
tail -F /work/ethereum/eth-parity/parity.log

# If it not running properly, you can debug it
docker stop eth-parity
docker rm eth-parity
docker run -it -v /work/ethereum/eth-parity/:/home/parity/.local/share/io.parity.ethereum/ -p 8545:8545 -p 30303:30303 --name eth-parity --entrypoint=/bin/bash parity:2.4.0-btcpool
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
docker run -it -v /work/ethereum/etc-parity/:/home/parity/.local/share/io.parity.ethereum/ -p 8545:8545 -p 30303:30303 --name etc-parity --restart always -d parity:2.4.0-btcpool

# Check if Parity is running properly
tail -F /work/ethereum/etc-parity/parity.log

# If it not running properly, you can debug it
docker stop etc-parity
docker rm etc-parity
docker run -it -v /work/ethereum/etc-parity/:/home/parity/.local/share/io.parity.ethereum/ -p 8545:8545 -p 30303:30303 --name etc-parity --entrypoint=/bin/bash parity:2.4.0-btcpool
```
