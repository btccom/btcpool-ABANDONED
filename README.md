BTCPool for BTC, BCH, UBTC & SBTC
==================

BTCPool is backend system of [https://pool.btc.com](https://pool.btc.com).

> This is a version of BTCPool that supports more SHA256 blockchains. Check the **single-blockchain** version at [legacy](https://github.com/btccom/btcpool/tree/legacy) branch.

The pool backend support 4 blockchains at current:
* [Bitcoin](https://bitcoin.org/)
* [BitcoinCash](https://bitcoincash.org/)
* [UnitedBitcoin](https://ub.com/)
* [SuperBitcoin](http://supersmartbitcoin.com/)

Note: The project is still a testbed and work in progress, all things could be changed.

See Also:
* [BTCPool's modules that writed by golang](https://github.com/btccom/btcpool-go-modules)

## Architecture (need update)

![Architecture](docs/btcpool.png)

## Install

1. Install `Zookeeper`, or see [INSTALL-ZooKeeper.md](docs/INSTALL-ZooKeeper.md)
  * [https://zookeeper.apache.org/](https://zookeeper.apache.org/)
2. Install `Kafka`, or see [INSTALL-Kafka.md](docs/INSTALL-Kafka.md)
  * [https://kafka.apache.org/](https://kafka.apache.org/)
3. Install `Bitcoind`, need to enable ZMQ
4. Install `BTCPool`, see [INSTALL-BTCPool.md](docs/INSTALL-BTCPool.md)

## Benchmark

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
