kafka Repeater
==================

* Forward Kafka messages from one topic to another.
* Forward Kafka messages from one cluster to another.
* Fetch `struct ShareBitcoin` (bitcoin share v2) messages from a Kafka topic, convert them to `struct Share` (bitcoin share v1 of legacy branch) and send to another topic.
* Modify the difficulty of Bitcoin Shares according to stratum jobs in kafka and send them to the other topic.

### build

```bash
mkdir build
cd build
cmake ..
make
```

### run

```bash
cp ../kafka_repeater.cfg .
./kafka_repeater -c kafka_repeater.cfg
./kafka_repeater -c kafka_repeater.cfg -l stderr
mkdir log
./kafka_repeater -c kafka_repeater.cfg -l log
```
