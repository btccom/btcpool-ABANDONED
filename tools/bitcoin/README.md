Maintenance, Upgrades, and Testing Tools for BTCPool
==================

### kafka_repeater

* [TODO] Forward Kafka messages from one topic to another.
* [TODO] Forward Kafka messages from one cluster to another.
* Fetch `struct ShareBitcoin` (deveth) messages from a Kafka topic, convert them to `struct Share` (master/legacy) and send to another topic.

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
