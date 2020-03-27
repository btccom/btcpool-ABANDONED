BTCPool for Bitcoin, Ethereum, Decred, Bytom, ...
==================

|Branch|Status|
|------|------|
|master|[![CircleCI](https://circleci.com/gh/btccom/btcpool/tree/master.svg?style=shield)](https://circleci.com/gh/btccom/btcpool/tree/master)|
|deveth|[![CircleCI](https://circleci.com/gh/btccom/btcpool/tree/deveth.svg?style=shield)](https://circleci.com/gh/btccom/btcpool/tree/deveth)|

BTCPool is backend system of [https://pool.btc.com](https://pool.btc.com).

> This is a version of BTCPool that supports more blockchains. Check the **SHA256-only** version at [master](https://github.com/btccom/btcpool/tree/master) branch.

The pool backend support these blockchains at current:
* SHA256
   * [Bitcoin](https://bitcoin.org/)
   * [BitcoinCash](https://bitcoincash.org/)
   * [UnitedBitcoin](https://ub.com/)
* Scrypt
   * [Litecoin](https://litecoin.org/)
* ETHash / Daggerhashimoto
   * [Ethereum](https://www.ethereum.org/)
   * [Ethereum Classic](https://ethereumclassic.org/)
* EquiHash
   * [ZCash](https://z.cash/)
   * [Beam](https://www.beam.mw/)
   > Tips: ZCash and BEAM use different EquiHash parameters and are not compatible in mining.
* Cuckoo Cycle
   * [Grin](https://grin-tech.org/)
   > Tips: Grin supports `Cuckaroo Cycle 29` and `Cuckatoo Cycle 31` at the same time, miners can choose an algorithm to mine.
* Blake-256
   * [Decred](https://www.decred.org/)
* Tensority
   * [Bytom](https://bytom.io/)
* Eaglesong
   * [Ckb](https://www.nervos.org/) (finished but the test is not enough)
   > Tips: only Tested with bminer and nbminer. need to be updated after the new version of ckb is released. 
* Others
   * ~~[Siacoin](https://www.sia.tech/)~~ (not finished and need test)

It also support these merged mining blockchains of SHA256 blockchains:
* SHA256 merged mining
   * [Namecoin](https://www.namecoin.org/)
   * [RSK](https://www.rsk.co/)
   * [ElastOS](https://elastos.org/)
   * Other blockchains that compatible with [Bitcoin merged mining specification](https://en.bitcoin.it/wiki/Merged_mining_specification)
   
If you want merged mine more than one chains that compatible with [Bitcoin merged mining specification](https://en.bitcoin.it/wiki/Merged_mining_specification), use [merged mining proxy](https://github.com/btccom/btcpool-go-modules/tree/master/mergedMiningProxy).

Note: The project is still a testbed and work in progress, all things could be changed.

See Also:
* [BTCPool's golang modules](https://github.com/btccom/btcpool-go-modules)

## Architecture (need update)

![Architecture](docs/btcpool.png)

## Install

1. Install `Zookeeper`, or see [INSTALL-ZooKeeper.md](docs/INSTALL-ZooKeeper.md)
  * [https://zookeeper.apache.org/](https://zookeeper.apache.org/)
2. Install `Kafka`, or see [INSTALL-Kafka.md](docs/INSTALL-Kafka.md)
  * [https://kafka.apache.org/](https://kafka.apache.org/)
3. Install `BTCPool`, see [INSTALL-BTCPool.md](docs/INSTALL-BTCPool.md)

## Upgrade

Upgrading to BTCPool 2.3.0 requires additional operations on `sharelogger`, `slparser` and `statshttpd` due to incompatible sharelog format changes.

See [UPGRADE-BTCPool.md](docs/UPGRADE-BTCPool.md) for more information.

## Benchmark (outdated)

We have test 100,000 miners online Benchmark. see [Benchmark-100000.md](docs/Benchmark-100000.md)

## BTCAgent

BTCAgent is a kind of stratum proxy which use customize protocol to communicate with the pool. It's very efficient and designed for huge mining farm.

* [AGENT.md](docs/AGENT.md)
* BTCAgent's [HomePage](https://github.com/btccom/BTCAgent)

## Testing

You could run `simulator` to test the system. It will simulate a lots of miners, need to enbale config `enable_simulator` on your Stratum Server.

## License
BTCPool is released under the terms of the MIT license. See [LICENSE](LICENSE) for more information or see [https://opensource.org/licenses/MIT](https://opensource.org/licenses/MIT).


Welcome aboard!

BTC.COM Team.
