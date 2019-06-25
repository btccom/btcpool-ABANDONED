BTCPool Docker Deploy Images
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
See [here](../base-image/).

## Build Deploy Images

### Use `build.sh`
```
# BTC
./build.sh -t btccom/btcpool-btc -b btccom/btcpool_build:btc-0.16.3 -j$(nproc)

# BCH
./build.sh -t btccom/btcpool-bch -b btccom/btcpool_build:bch-0.18.5 -j$(nproc)

# UBTC
./build.sh -t btccom/btcpool-ubtc -b btccom/btcpool_build:ubtc-2.5.0.1-1 -j$(nproc)

# SBTC (outdated)
./build.sh -t btccom/btcpool-sbtc -b btccom/btcpool_build:sbtc-0.16.2 -j$(nproc)

# LTC
./build.sh -t btccom/btcpool-ltc -b btccom/btcpool_build:ltc-0.16.3 -j$(nproc)

# ZEC
./build.sh -t btccom/btcpool-zec -b btccom/btcpool_build:zec-2.0.4 -j$(nproc)

# Other chains (ETH, Beam, Grin, Decred, Bytom, ...)
# Please use BTC's image.
```

### Use `docker build`
```
# BTC
docker build -t btccom/btcpool-btc -f Dockerfile --build-arg BASE_IMAGE=btccom/btcpool_build:btc-0.16.3 --build-arg BUILD_JOBS=$(nproc) --build-arg GIT_DESCRIBE=$(git describe --tag --long) ../../..

# BCH
docker build -t btccom/btcpool-bch -f Dockerfile --build-arg BASE_IMAGE=btccom/btcpool_build:bch-0.18.5 --build-arg BUILD_JOBS=$(nproc) --build-arg GIT_DESCRIBE=$(git describe --tag --long) ../../..

# UBTC
docker build -t btccom/btcpool-ubtc -f Dockerfile --build-arg BASE_IMAGE=btccom/btcpool_build:ubtc-2.5.0.1-1 --build-arg BUILD_JOBS=$(nproc) --build-arg GIT_DESCRIBE=$(git describe --tag --long) ../../..

# SBTC (outdated)
docker build -t btccom/btcpool-sbtc -f Dockerfile --build-arg BASE_IMAGE=btccom/btcpool_build:sbtc-0.16.2 --build-arg BUILD_JOBS=$(nproc) --build-arg GIT_DESCRIBE=$(git describe --tag --long) ../../..

# LTC
docker build -t btccom/btcpool-ltc -f Dockerfile --build-arg BASE_IMAGE=btccom/btcpool_build:ltc-0.16.3 --build-arg BUILD_JOBS=$(nproc) --build-arg GIT_DESCRIBE=$(git describe --tag --long) ../../..

# ZEC
docker build -t btccom/btcpool-zec -f Dockerfile --build-arg BASE_IMAGE=btccom/btcpool_build:zec-2.0.4 --build-arg BUILD_JOBS=$(nproc) --build-arg GIT_DESCRIBE=$(git describe --tag --long) ../../..

# Other chains (ETH, Beam, Grin, Decred, Bytom, ...)
# Please use BTC's image.
```

## Run unittest

```
# BTC
docker run -it --rm btccom/btcpool-btc unittest

# BCH
docker run -it --rm btccom/btcpool-bch unittest

# UBTC
docker run -it --rm btccom/btcpool-ubtc unittest

# SBTC (outdated)
docker run -it --rm btccom/btcpool-sbtc unittest

# LTC
docker run -it --rm btccom/btcpool-ltc unittest

# ZEC
docker run -it --rm btccom/btcpool-zec unittest

# Other chains (ETH, Beam, Grin, Decred, Bytom, ...)
# Same as BTC's command.
```

## Example: Running sserver

An example to show how to run a BTCPool's module in docker.

```
mkdir /work/config
cp '../../../src/eth/cfg/sserver(eth).cfg' '/work/config/sserver.cfg'

# Run container as a foreground process
docker run --rm --network=host -v=/work/config:/work/config btccom/btcpool-btc sserver -c /work/config/sserver.cfg

# Run container as a daemon
docker run --name=eth-sserver --network=host --restart=always -v=/work/config:/work/config -d btccom/btcpool-btc sserver -c /work/config/sserver.cfg

# Show logs
docker logs btc-sserver
```
