BTCPool Docker Base Images
============================

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

## Build Base Images

```
# BTC
docker build -t btccom/btcpool_build:btc-0.16.3 -f Dockerfile.btc --build-arg BUILD_JOBS=$(nproc) .

# BCH
docker build -t btccom/btcpool_build:bch-0.18.5 -f Dockerfile.bch --build-arg BUILD_JOBS=$(nproc) .

# BSV
docker build -t btccom/btcpool_build:bsv-1.0.1 -f Dockerfile.bsv --build-arg BUILD_JOBS=$(nproc) .

# UBTC
docker build -t btccom/btcpool_build:ubtc-2.5.0.1-1 -f Dockerfile.ubtc --build-arg BUILD_JOBS=$(nproc) .

# LTC
docker build -t btccom/btcpool_build:ltc-0.16.3 -f Dockerfile.ltc --build-arg BUILD_JOBS=$(nproc) .

# ZEC
docker build -t btccom/btcpool_build:zec-2.0.4 -f Dockerfile.zec --build-arg BUILD_JOBS=$(nproc) .

# Other chains (ETH, Beam, Grin, Decred, Bytom, ...)
# Please use BTC's base image.
```
