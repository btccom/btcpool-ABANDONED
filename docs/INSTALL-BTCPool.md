Install BTC.COM Pool
=====================

**Supported OS**
* `Ubuntu 14.04 LTS, 64 Bits`
* `Ubuntu 16.04 LTS, 64 Bits`
* `macOS Sierra or later`

**BTCPool has a known compatibility issue with 32-bit operating systems. https://github.com/btccom/btcpool/issues/38**

If you want to run BTCPool on a 32-bit operating system, you must fix the problem first. Or, choose a 64-bit operating system.

## Build as a Docker Image (recommend)

See [this page](../docker/btcpool/deploy).

## Build Manually

### Install dependency

#### CentOS 7

See [this wiki](https://github.com/btccom/btcpool/wiki/Build-BTCPool-on-CentOS-7-%7C-%E5%9C%A8CentOS7%E4%B8%8A%E6%9E%84%E5%BB%BABTCPool).

#### Ubuntu
```bash
apt-get update
apt-get install -y build-essential autotools-dev libtool autoconf automake pkg-config cmake \
                   openssl libssl-dev libcurl4-openssl-dev libconfig++-dev \
                   libboost-all-dev libgmp-dev libmysqlclient-dev libzookeeper-mt-dev \
                   libzmq3-dev libgoogle-glog-dev libhiredis-dev zlib1g zlib1g-dev \
                   libsodium-dev libprotobuf-dev protobuf-compiler
```

Notice: It is no longer recommended to install `libevent-dev` from the software source.
**The release and stable version of libevent will cause a dead lock bug in sserver** ([issue #75](https://github.com/btccom/btcpool/issues/75)).
It is recommended that you manually build the libevent from its master branch with commands at below.

Sometimes one or two packages will fail due to dependency problems, and you can try `aptitude`.
```bash
apt-get update
apt-get install -y aptitude

aptitude install build-essential autotools-dev libtool autoconf automake pkg-config cmake \
                   openssl libssl-dev libcurl4-openssl-dev libconfig++-dev \
                   libboost-all-dev libgmp-dev libmysqlclient-dev libzookeeper-mt-dev \
                   libzmq3-dev libgoogle-glog-dev libhiredis-dev zlib1g zlib1g-dev \
                   libsodium-dev libprotobuf-dev protobuf-compiler

# Input `n` if the solution is `NOT INSTALL` some package.
# Eventually aptitude will give a solution that downgrade some packages to allow all packages to be installed.
```

* build libevent from its master branch

Notice: **libevent before release-2.1.9-beta will cause a dead lock bug in sserver** ([issue #75](https://github.com/btccom/btcpool/issues/75)). Please use **release-2.1.9-beta** and later.
```
wget https://github.com/libevent/libevent/releases/download/release-2.1.10-stable/libevent-2.1.10-stable.tar.gz
tar zxf libevent-2.1.10-stable.tar.gz
cd libevent-2.1.10-stable
./autogen.sh
./configure --disable-shared
make -j$(nproc) && make install
```

* build librdkafka-v0.9.1

```bash
wget https://github.com/edenhill/librdkafka/archive/0.9.1.tar.gz
tar zxvf 0.9.1.tar.gz
cd librdkafka-0.9.1
./configure && make -j$(nproc) && make install

# if you want to keep static libraries only
rm -v /usr/local/lib/librdkafka*.so /usr/local/lib/librdkafka*.so.*
```

#### macOS

Please install [brew](https://brew.sh/) first.

```bash
brew install cmake openssl libconfig boost mysql zmq gmp libevent zookeeper librdkafka hiredis
```

* glog-v0.3.4

```
wget https://github.com/google/glog/archive/v0.3.4.tar.gz
tar zxvf v0.3.4.tar.gz
cd glog-0.3.4
./configure && make && make install
```

### Build BTCPool

#### CMake Options

There are some cmake options that can change the behavior of compiler,
enable or disable optional features of BTCPool,
or link BTCPool with different blockchains.

|   Option Name    |     Values     | Default Value |   Description  |
|         -        |        -       |      -        |        -       |
| CMAKE_BUILD_TYPE | Release, Debug |    Release    |   Build type   |
| JOBS | A number, between 1 to your CPU cores' number. | 1 | Concurrent jobs when building blockchain's source code that linking to BTCPool. |
| CHAIN_TYPE | BTC, BCH, BSV, UBTC, SBTC, LTC, ZEC | No default value, you must define it. | Blockchain's type that you want to BTCPool linking to. |
| CHAIN_SRC_ROOT | A path of dir, such as `/work/bitcoin`. | No default value, you must define it. | The path of blockchain's source code that you want to BTCPool linking to. |
| OPENSSL_ROOT_DIR | A path of dir, such as `/usr/local/opt/openssl`. | No definition is required by default, and cmake will automatically look for it. | The path of `openssl`'s source code. The macOS user may need to define it if cmake cannot find it automatically. |
| POOL__WORK_WITH_STRATUM_SWITCHER | ON, OFF | OFF | Build a special version of pool's stratum server, so you can run it with a stratum switcher. See also: [Stratum Switcher](https://github.com/btccom/btcpool-go-modules/stratumSwitcher). |
| POOL__USER_DEFINED_COINBASE | ON, OFF | OFF | Build a special version of pool that allows user-defined content to be inserted into coinbase input. TODO: add documents about it. |
| POOL__USER_DEFINED_COINBASE_SIZE | A number (bytes), from 1 to the maximum length that coinbase input can hold | 10 | The size of user-defined content that inserted into coinbase input. No more than 20 bytes is recommended. |
| POOL__INSTALL_PREFIX | A path of dir, such as `/work/btcpool.btc`. | /work/bitcoin.\[btc\|bch\|sbtc\|ubtc\] | The install path of `make install`. The deb package that generated by `make package` will install to the same path. |
| POOL__GENERATE_DEB_PACKAGE | ON, OFF | OFF | When it enabled, you can generate a deb package with `make package`. |

#### Building commands (example)

**build BTCPool that linking to Bitcoin**

```bash
mkdir /work
cd /work
wget -O bitcoin-0.16.0.tar.gz https://github.com/bitcoin/bitcoin/archive/v0.16.0.tar.gz
tar zxf bitcoin-0.16.0.tar.gz

git clone https://github.com/btccom/btcpool.git
cd btcpool
mkdir build
cd build

# Release build:
cmake -DJOBS=4 -DCHAIN_TYPE=BTC -DCHAIN_SRC_ROOT=/work/bitcoin-0.16.0 ..
make -j$(nproc)

# Release build at macOS:
cmake -DCHAIN_TYPE=BTC -DCHAIN_SRC_ROOT=/work/bitcoin-0.16.0 -DOPENSSL_ROOT_DIR=/usr/local/opt/openssl ..
make -j$(nproc)

# Debug build:
cmake -DCMAKE_BUILD_TYPE=Debug -DCHAIN_TYPE=BTC -DCHAIN_SRC_ROOT=/work/bitcoin-0.16.0 ..
make -j$(nproc)
```

**build BTCPool that linking to BitcoinCash ABC**

```bash
mkdir /work
cd /work
wget -O bitcoin-abc-0.18.5.tar.gz https://github.com/Bitcoin-ABC/bitcoin-abc/archive/v0.18.5.tar.gz
tar zxf bitcoin-abc-0.18.5.tar.gz

git clone https://github.com/btccom/btcpool.git
cd btcpool
mkdir build
cd build

# Release build:
cmake -DJOBS=4 -DCHAIN_TYPE=BCH -DCHAIN_SRC_ROOT=/work/bitcoin-abc-0.18.5 ..
make -j$(nproc)

# Release build at macOS:
cmake -DCHAIN_TYPE=BCH -DCHAIN_SRC_ROOT=/work/bitcoin-abc-0.18.5 -DOPENSSL_ROOT_DIR=/usr/local/opt/openssl ..
make -j$(nproc)

# Debug build:
cmake -DCMAKE_BUILD_TYPE=Debug -DCHAIN_TYPE=BCH -DCHAIN_SRC_ROOT=/work/bitcoin-abc-0.18.5 ..
make -j$(nproc)
```

Note: `bitcoin-abc-0.17.1` and earlier are incompatible with current BTCPool, you will meet this error:
> /work/bitcoin-abc-0.17.1/src/crypto/libbitcoin_crypto_base.a not exists!

**build BTCPool that linking to Bitcoin SV**

```bash
mkdir /work
cd /work
wget -O bitcoin-sv-0.1.0.tar.gz https://github.com/bitcoin-sv/bitcoin-sv/archive/v0.1.0.tar.gz
tar zxf bitcoin-sv-0.1.0.tar.gz

git clone https://github.com/btccom/btcpool.git
cd btcpool
mkdir build
cd build

# Release build:
cmake -DJOBS=4 -DCHAIN_TYPE=BSV -DCHAIN_SRC_ROOT=/work/bitcoin-sv-0.1.0 ..
make -j$(nproc)

# Release build at macOS:
cmake -DCHAIN_TYPE=BSV -DCHAIN_SRC_ROOT=/work/bitcoin-sv-0.1.0 -DOPENSSL_ROOT_DIR=/usr/local/opt/openssl ..
make -j$(nproc)

# Debug build:
cmake -DCMAKE_BUILD_TYPE=Debug -DCHAIN_TYPE=BSV -DCHAIN_SRC_ROOT=/work/bitcoin-sv-0.1.0 ..
make -j$(nproc)
```

**build BTCPool that linking to UnitedBitcoin**

```bash
mkdir /work
cd /work
# Notice: v2.5.0.1-1.tar.gz from UnitedBitcoin official is wrong (missing one commit),
#         So here we use the release of our own fork.
wget -O UnitedBitcoin-2.5.0.1-1.tar.gz https://github.com/btccom/UnitedBitcoin/archive/v2.5.0.1-1.tar.gz
tar zxf UnitedBitcoin-2.5.0.1-1.tar.gz

# install libdb that UnitedBitcoin's wallet required
apt-get install -y software-properties-common
add-apt-repository -y ppa:bitcoin/bitcoin
apt-get -y update
apt-get install -y libdb4.8-dev libdb4.8++-dev

# UnitedBitcoin will build failed with `--disable-wallet`, so we have to build it manually with wallet
cd UnitedBitcoin-2.5.0.1-1
./autogen.sh
./configure --disable-bench --disable-tests
make -j$(nproc)

cd ..
git clone https://github.com/btccom/btcpool.git
cd btcpool
mkdir build
cd build

# Release build:
cmake -DJOBS=4 -DCHAIN_TYPE=UBTC -DCHAIN_SRC_ROOT=/work/UnitedBitcoin-2.5.0.1-1 ..
make -j$(nproc)

# Release build at macOS:
cmake -DCHAIN_TYPE=UBTC -DCHAIN_SRC_ROOT=/work/UnitedBitcoin-2.5.0.1-1 -DOPENSSL_ROOT_DIR=/usr/local/opt/openssl ..
make -j$(nproc)

# Debug build:
cmake -DCMAKE_BUILD_TYPE=Debug -DCHAIN_TYPE=UBTC -DCHAIN_SRC_ROOT=/work/UnitedBitcoin-2.5.0.1-1 ..
make -j$(nproc)
```

**build BTCPool that linking to SuperBitcoin**

**Warning**: Support for SuperBitcoin is **outdated** and lacks maintenance.
Existing code may **not be compatible** with the current SuperBitcoin blockchain.

In addition, if you have a plan to maintain the SuperBitcoin supporting,
you are welcome to make a pull request.

```bash
mkdir /work
cd /work
wget -O SuperBitcoin-0.17.1.tar.gz https://github.com/superbitcoin/SuperBitcoin/archive/v0.17.1.tar.gz
tar zxf SuperBitcoin-0.17.1.tar.gz

git clone https://github.com/btccom/btcpool.git
cd btcpool
mkdir build
cd build

# Release build:
cmake -DJOBS=4 -DCHAIN_TYPE=SBTC -DCHAIN_SRC_ROOT=/work/SuperBitcoin-0.17.1 ..
make -j$(nproc)

# Release build at macOS:
cmake -DCHAIN_TYPE=SBTC -DCHAIN_SRC_ROOT=/work/SuperBitcoin-0.17.1 -DOPENSSL_ROOT_DIR=/usr/local/opt/openssl ..
make -j$(nproc)

# Debug build:
cmake -DCMAKE_BUILD_TYPE=Debug -DCHAIN_TYPE=SBTC -DCHAIN_SRC_ROOT=/work/SuperBitcoin-0.17.1 ..
make -j$(nproc)
```

**build BTCPool that linking to Litecoin**

```bash
mkdir /work
cd /work
wget -O litecoin-0.16.3.tar.gz https://github.com/litecoin-project/litecoin/archive/v0.16.3.tar.gz
tar zxf litecoin-0.16.3.tar.gz

git clone https://github.com/btccom/btcpool.git
cd btcpool
mkdir build
cd build

# Release build:
cmake -DJOBS=4 -DCHAIN_TYPE=LTC -DCHAIN_SRC_ROOT=/work/litecoin-0.16.3 ..
make -j$(nproc)

# Debug build:
cmake -DCMAKE_BUILD_TYPE=Debug -DCHAIN_TYPE=LTC -DCHAIN_SRC_ROOT=/work/litecoin-0.16.3 ..
make -j$(nproc)
```

**build BTCPool that linking to ZCash**

```bash
mkdir /work
cd /work
wget -O zcash-2.0.4.tar.gz https://github.com/zcash/zcash/archive/v2.0.4.tar.gz
tar zxf zcash-2.0.4.tar.gz

git clone https://github.com/btccom/btcpool.git
cd btcpool
mkdir build
cd build

# Release build:
cmake -DJOBS=4 -DCHAIN_TYPE=ZEC -DCHAIN_SRC_ROOT=/work/zcash-2.0.4 ..
make -j$(nproc)

# Debug build:
cmake -DCMAKE_BUILD_TYPE=Debug -DCHAIN_TYPE=ZEC -DCHAIN_SRC_ROOT=/work/zcash-2.0.4 ..
make -j$(nproc)
```

**How about ETH/ETC/Beam/Grin/Decred/Bytom?**

Please use the build that linking to Bitcoin (`-DCHAIN_TYPE=BTC`).

---

## Init btcpool

### Init the Running Folders

Now create folder for btcpool, if you are going to run all service in one machine you could run `install/init_folders.sh` as below.

```
cd /work/btcpool/build
bash ../install/init_folders.sh
```

### Setup Full-Nodes
Before starting btcpool's services, at least one bitcoin full-node needs to be setup. Which full-node to use depends on the blockchain you want to mining.

Also start Rsk node or Namecoin node if merged mining for any of those chains.

The following are some dockerfiles of full-nodes:

* Bitcoin: [docker for Bitcoin Core](../docker/bitcoind)
* BitcoinCash: [docker for Bitcoin ABC](../docker/bitcoin-abc)
* UnitedBitcoin: [docker for UnitedBitcoin](../docker/united-bitcoin)
* SuperBitcoin: [docker for SuperBitcoin](../docker/super-bitcoin)
* Litecoin: [docker for Litecoin Core](../docker/litecoind)
* Namecoin: [docker for Namecoin Core](../docker/namecoind)
* RSK: [docker for RSKJ](https://github.com/rsksmart/rskj/wiki/install-rskj-using-docker)
* Ethereum:
   * [docker for Parity](../docker/eth-parity)
   * [docker for Geth](../docker/eth-geth)
* Decred: [docker for dcrd](../docker/dcrd)
* Beam: [docker for Beam Node](../docker/beam-node)

If you want to merge-mining more than one chains that follow [Bitcoin Merged Mining Specification](https://en.bitcoin.it/wiki/Merged_mining_specification) (likes Namecoin, ElastOS, etc), you can running a [Merged Mining Proxy](https://github.com/btccom/btcpool-go-modules/tree/master/mergedMiningProxy) and let the pool's `nmcauxmaker` connect to it.

### Configure Merged Mining

See [MergedMining.md](MergedMining.md) for more details of bitcoin/litecoin merged mining.

### Init MySQL Databases & Tables
The pool's `statshttpd`, `slparser` and `blkmaker` will write miners', users' & blockchains' information to mysql. We recommend that you create two databases and import the corresponding tables.

```bash
# create database `bpool_local_db`, `bpool_local_stats_db`
# and import tables with mysql command-line client.
cd /work/btcpool/install
mysql -h xxx -u xxx -p
```
```sql
CREATE DATABASE bpool_local_db;
USE bpool_local_db;
-- for BTC/BCH/BSV/UBTC/SBTC/LTC/ZEC
SOURCE bpool_local_db.sql;
-- for other chains (use one of them)
SOURCE bpool_local_db_BEAM.sql;
SOURCE bpool_local_db_Bytom.sql;
SOURCE bpool_local_db_Decred.sql;
SOURCE bpool_local_db_ETH.sql;
SOURCE bpool_local_db_Grin.sql;

CREATE DATABASE bpool_local_stats_db;
USE bpool_local_stats_db;
SOURCE bpool_local_stats_db.sql;
```

### Install Redis (Optional)

If you want to install Redis as your `statshttpd`'s optional storage, refer to [INSTALL-Redis.md](INSTALL-Redis.md).

### User list API

You need to implement a HTTP API for stratum server (`sserver`). The API is use to fetch user list in incremental model. Stratum Server will call this API every few seconds, interval is about 10 seconds. When Stratum Server start, it will fetch again and again util no more users.

Should works as below.

```
#
# `last_id` is the offset. pagesize is 5. total user count is 8.
#
$ curl http://xxx.xxx.xxx/GetUserList?last_id=0
{"err_no":0,"err_msg":null,"data":{"litianzhao":1,"spica":2,"fazhu":3,"kevin":4,"kevin1":5}}

# fetch next page
$ curl http://xxx.xxx.xxx/GetUserList?last_id=5
{"err_no":0,"err_msg":null,"data":{"kevin2":6,"kevin3":7,"Miles":8}}

# fetch next page, no more users
$ curl http://xxx.xxx.xxx/GetUserList?last_id=8
{"err_no":0,"err_msg":null,"data":{}}
```

There is a demo for the API: https://github.com/btccom/btcpool/issues/16#issuecomment-278245381

There are more reference implementations here, including a user list with automatic registration enabled
and a user list with `POOL__USER_DEFINED_COINBASE=ON`: [install/api-sample](../install/api-sample)

### use `supervisor` to manager services.

```
#
# install supervisor
#
apt-get install supervisor

#
# change open file limits
# (add 'minfds=65535' in the [supervisord] section of supervisord.conf)
#
cat /etc/supervisor/supervisord.conf | grep -v 'minfds\s*=' | sed '/\[supervisord\]/a\minfds=65535' | tee /etc/supervisor/supervisord.conf
#
# maker the change effective
#
service supervisor restart


#
# copy processes' config files for supervisor
#
cd /work/btcpool/build/install/supervisord/

################################################################################
# for common server
################################################################################
cp gbtmaker.conf      /etc/supervisor/conf.d
cp jobmaker.conf      /etc/supervisor/conf.d
cp blkmaker.conf      /etc/supervisor/conf.d
cp sharelogger.conf   /etc/supervisor/conf.d
#
# if you need to enable watch other pool's stratum job. poolwatcher will 
# generate empty getblocktemplate when they find a new height stratum job 
# from other pools. this could reduce pool's orphan block rate.
#
cp poolwatcher.conf   /etc/supervisor/conf.d
#
# enable namecoin merged mining
#
cp nmcauxmaker.conf   /etc/supervisor/conf.d
#
# enable rsk merged mining
#
cp ../install/supervisord/gwmaker.conf   /etc/supervisor/conf.d

#
# start services: gbtmaker, jobmaker, blkmaker, sharelogger...
#
$ supervisorctl
> reread
> update
> status


#
# start your stratum server(see below) and add some miners to the pool, after make -j$(nproc)
# some shares than start 'slparser' & 'statshttpd'
#
cp slparser.conf      /etc/supervisor/conf.d
cp statshttpd.conf    /etc/supervisor/conf.d

# start services: slparser, statshttpd
$ supervisorctl
> reread
> update
> status


################################################################################
# for stratum server
################################################################################
cp sserver.conf       /etc/supervisor/conf.d

$ supervisorctl
> reread
> update
> status
```

### mining-coin switcher

[StratumSwitcher](https://github.com/btccom/btcpool-go-modules/blob/master/stratumSwitcher) is a BTCPool's module that writed by golang. It provides a per user's mining-coin switching capability controlled by the user self or a pool administrator.

There are 3 processes to provide the capability:
* ~~[StratumSwitcher](https://github.com/btccom/btcpool-go-modules/blob/master/stratumSwitcher)~~ (Deprecated. Use sserver itself to achieve the same features.)
* [Switcher API Server](https://github.com/btccom/btcpool-go-modules/blob/master/switcherAPIServer)
* [Init User Coin](https://github.com/btccom/btcpool-go-modules/blob/master/initUserCoin)

A zookeeper is needed as a notification channel, and a [new user-coin-map API](https://github.com/btccom/btcpool-go-modules/blob/master/switcherAPIServer/README.md) need be provided to the `Switcher API Server`.

The module currently has only Chinese documents. Welcome to translate it and initiate a pull request.

> Tips: `sserver` currently has all features of `StratumSwitcher` built in and is more stable than `StratumSwitcher`. Therefore, it is not recommended to use `StratumSwitcher` anymore.
> 
> But you still need the `Switcher API Server` and `Init User Coin` from the [btcpool-go-modules](https://github.com/btccom/btcpool-go-modules) repo to achieve the chain switching.
> 
> Here is an example of a sserver configuration file that supports chain switching:
> [sserver(multi-chains).cfg](../src/bitcoin/cfg/sserver(multi-chains).cfg).

## Upgrade btcpool

Check release note and upgrade guide. Maybe add some config options to config file or change something of database table. 

Get the latest codes and rebuild:

```
cd /work/btcpool/build
git pull
cmake ..
make -j$(nproc)
```

use `supervisorctl` to restart your services:

```
$ supervisorctl
> restart xxxx
```

### Incompatible upgrade

Upgrading to BTCPool 2.3.0 requires additional operations on `sharelogger`, `slparser` and `statshttpd` due to incompatible sharelog format changes.

See [UPGRADE-BTCPool.md](./UPGRADE-BTCPool.md) for more information.
