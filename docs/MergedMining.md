# BTCPool Merged Mining

BTCPool supports three different forms of merged mining, which are AuxPow, RSK and VCash.

Please note that RSK and VCash merged mining do not use the AuxPow standard and require separate configuration.


## AuxPow Merged Mining
The [Bitcoin Merged Mining Specification](https://en.bitcoin.it/wiki/Merged_mining_specification) defines the AuxPow merged mining. In simple terms, it places the block hash of another blockchain into the parent blockchain's coinbase transaction input. If you want to merged mine multiple chains at the same time, you need to combine their hashes into a merkle tree and put the merkle root into the coinbase input.

AuxPow merged mining is available in the following blockchains (incomplete):

| Blockchain | Parent Chain Algorithm | Frequently-Used Parent Chain |
| :--------: | :--------------------: | :--------------------------: |
| NameCoin   | SHA256                 | BTC, BCH, UBTC, BSV          |
| ELastos    | SHA256                 | BTC, BCH, UBTC, BSV          |
| Unobtanium | SHA256                 | BTC, BCH, UBTC, BSV          |
| Dogecoin   | Scrypt                 | LTC                          |

### Merged Mining a Single AuxPow Blockchain

If you only need to merged mine one AuxPow node, you only need to configure `nmcauxmaker` to connect to the node of that blockchain, then configure `auxpow_gw_topic` in [jobmaker.cfg](../src/bitcoin/cfg/jobmaker.cfg) (consistent with `auxpow_gw_topic` in [nmcauxmaker.cfg](../src/nmcauxmaker/nmcauxmaker.cfg)).

Notice: The `rpc_addr` and `rpc_userpwd` of the AuxPow blockchain will be placed into the job and read by `blkmaker`. Please ensure that `blkmaker` can connect to this node with the same configuration as `nmcauxmaker`, otherwise the merged mining block will not be able to submit.

TODO: remove `nmc` prefix of the program name. It is not only used to mine `Namecoin`, but to mine all AuxPow blockchains.

### Merged Mining more than one AuxPow Blockchains

You need a [Merged Mining Proxy](https://github.com/btccom/btcpool-go-modules/tree/master/mergedMiningProxy). Let your `nmcauxmaker` connect to the `Merged Mining Proxy`, and the proxy will connect to each blockchain node (such as `Namecoin` and `Elastos`).

The configuration section `RPCServer` of `Merged Mining Proxy` used to configure an RPC server which a `nmcauxmaker` can connect to. Then add your AuxPow blockchains to its `Chains` configuration section.

TODO: We need volunteers to translate the documentation of the [Merged Mining Proxy](https://github.com/btccom/btcpool-go-modules/tree/master/mergedMiningProxy) from Chinese to English. Welcome to submit a pull request to us.


## RSK Merged Mining

The RSK puts its block hash into the coinbase output of a SHA256 parent chain. You can use any SHA256 parent chain to merged mine RSK, including BTC, BCH, UBTC, BSV.

To mine RSK, you need to configure gwmaker (`chain_type="RSK"`). Then configure `rsk_rawgw_topic` in [jobmaker.cfg](../src/bitcoin/cfg/jobmaker.cfg) (consistent with `rawgw_topic` in [gwmaker.cfg](../src/gwmaker/gwmaker.cfg)).

Notice: The `rpc_addr` and `rpc_userpwd` of the RSK node will be placed into the job and read by `blkmaker`. Please ensure that `blkmaker` can connect to this node with the same configuration as `gwmaker`, otherwise the merged mining block will not be able to submit.


## VCash Merged Mining

The VCash puts its block hash into the coinbase output of a SHA256 parent chain. You can use any SHA256 parent chain to merged mine VCash, including BTC, BCH, UBTC, BSV.

To mine VCash, you need to configure gwmaker (`chain_type="VCASH"`). Then configure `vcash_rawgw_topic` in [jobmaker.cfg](../src/bitcoin/cfg/jobmaker.cfg) (consistent with `rawgw_topic` in [gwmaker.cfg](../src/gwmaker/gwmaker.cfg))).

Notice:
1. Both your blkmaker and your jobmaker need to be upgraded to a version that supports VCash merged mining, otherwise the VCash block will not be submitted.
2. The `rpc_addr` and `rpc_userpwd` of the VCash node will be placed into the job and read by `blkmaker`. Please ensure that `blkmaker` can connect to this node with the same configuration as `gwmaker`, otherwise the merged mining block will not be able to submit.

