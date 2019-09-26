Docker for go-ethereum
============================

* OS: `Ubuntu 16.04 LTS`
* Docker image: build by yourself
* geth version: v1.8.15 with RPC eth_submitWorkDetail

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
vim /eth/docker/daemon.json
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
git clone --depth=1 -b submit_work_detail https://github.com/btccom/go-ethereum.git
cd go-ethereum
docker build -t eth-geth:1.8.15-submit_work_detail .
```

## Create Config Files

### Command Line Options

```
mkdir -p /work/ethereum/eth-geth
vim /work/ethereum/eth-geth/config.sh
```

```
geth \
--rpc \
--rpcaddr='0.0.0.0' \
--rpcvhosts='*' \
--mine \
--etherbase='<your ethereum address>' \
--extradata='Project BTCPool' \
--cache=2048 \
--maxpeers=200 \
>> /root/.ethereum/debug.log 2>&1
```

Tips:
* It's a shell script for running `geth`.
* Suggested format:
  - The first line is the program name `geth`.
  - One configuration per line, ending with `\`.
  - The last line is a output redirection or a single semicolon (`;`) if you don't want a log file.

#### Why is it a shell script instead of a configuration file?

@YihaoPeng wrote on September 4, 2018:

<blockquote>
At the beginning, I want to use the configuration file (`--config=xxx.toml`) of `geth`.

However, [a decision](https://blog.ethereum.org/2017/04/14/geth-1-6-puppeth-master/) by `geth`'s developer made my plan unsuccessful:
> Certain fields have been omitted to prevent sensitive data circulating in configuration files.

So `--extradata` is omitted... But it is not sensitive at all, it will be public in each block you mined!

Although very unhappy, I have no choice. I can only switch to using the shell script.

In addition, writing configuration files directly is difficult due to the lack of official documentation.
You can only write the command line options first, then use the `dumpconfig` command to generate a configuration file.

So why not just use the shell script directly? Although it's not so readable, it has good documentation,
can be written directly, and can controls all the features you want to control.

However, I still hope to have a regular and convenient configuration file.

![](https://cloud.githubusercontent.com/assets/824194/20584597/69ca597c-b1b1-11e6-9461-4bbd1f88a211.jpeg)
</blockquote>

### Static Nodes

```
mkdir -p /work/ethereum/eth-geth/geth/
vim /work/ethereum/eth-geth/geth/static-nodes.json
```

```
[
  "enode://f4642fa65af50cfdea8fa7414a5def7bb7991478b768e296f5e4a54e8b995de102e0ceae2e826f293c481b5325f89be6d207b003382e18a8ecba66fbaf6416c0@33.4.2.1:30303",
  "enode://pubkey@ip:port"
]
```

## Start Docker Container

```
# start docker
docker run -it -v /work/ethereum/eth-geth/:/root/.ethereum/ -p 8545:8545 -p 30303:30303 --name eth-geth --restart always -d eth-geth:1.8.15-submit_work_detail

# see the log
tail -f /work/ethereum/eth-geth/geth.log

# login
docker exec -it eth-geth sh

# If it not running properly, you can debug it
docker stop eth-geth
docker rm eth-geth
docker run -it -v /work/ethereum/eth-geth/:/root/.ethereum/ -p 8545:8545 -p 30303:30303 --name eth-geth --entrypoint=/bin/sh eth-geth:1.8.15-submit_work_detail
```
