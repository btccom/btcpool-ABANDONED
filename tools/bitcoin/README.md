Maintenance, Upgrades, and Testing Tools for BTCPool
==================

### kafka repeater

* [TODO] Forward Kafka messages from one topic to another.
* [TODO] Forward Kafka messages from one cluster to another.
* Fetch `struct ShareBitcoin` (bitcoin share v2) messages from a Kafka topic, convert them to `struct Share` (bitcoin share v1 of legacy branch) and send to another topic.

#### build

```bash
mkdir kafka_repeater/build
cd kafka_repeater/build
cmake ..
make
```

#### run

```bash
cp ../kafka_repeater.cfg .
./kafka_repeater -c kafka_repeater.cfg
./kafka_repeater -c kafka_repeater.cfg -l stderr
mkdir log
./kafka_repeater -c kafka_repeater.cfg -l log
```

### sharelog file convertor

* Convert a Bitcoin sharelog v2 file to sharelog v1, then it can be handled by a legacy slparser.

#### build

```bash
mkdir share_convertor/build
cd share_convertor/build
cmake ..
make
```

#### run

```bash
./share_convertor -i /work/sharelogv2/sharelogBCH-2018-08-27.bin -o /work/sharelogv1/sharelog-2018-08-27.bin
```
