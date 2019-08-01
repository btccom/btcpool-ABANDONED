kafka Repeater
==================

* Forward Kafka messages from one topic to another.
* Forward Kafka messages from one cluster to another.
* Fetch `struct ShareBitcoin` (bitcoin share v2) messages from a Kafka topic, convert them to `struct Share` (bitcoin share v1 of legacy branch) and send to another topic.
* Modify the difficulty of Bitcoin Shares according to stratum jobs in kafka and send them to the other topic.

### build

```bash
apt-get update && apt-get install -y \
    autoconf \
    automake \
    build-essential \
    cmake \
    libboost-all-dev \
    libconfig++-dev \
    libgoogle-glog-dev \
    libssl-dev \
    libtool \
    libzookeeper-mt-dev \
    libsasl2-dev \
    pkg-config \
    python \
    wget \
    zlib1g-dev

# Build librdkafka static library
# Remove dynamic libraries of librdkafka
# In this way, the constructed deb package will
# not have dependencies that not from software sources.
wget https://github.com/edenhill/librdkafka/archive/v1.1.0.tar.gz
tar zxf v1.1.0.tar.gz
cd librdkafka-1.1.0
./configure && make && make install
cd /usr/local/lib && find . | grep 'rdkafka' | grep '.so' | xargs rm

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

### SSL+SASL Authorize (only for out_brokers)

```cfg
kafka = {
    in_brokers = "127.0.0.1:9092";
    in_topic = "BtcShare";
    # Used to record progress / offsets.
    # Change it to reset the progress (will forward from the beginning).
    # The two repeater cannot have the same group id, otherwise the result is undefined.
    in_group_id = "btc_forward_1";

    out_brokers = "remote-host:9092";
    out_topic = "Share";

    # authorize settings (only for out_brokers)
    #
    # To generate a key and a self-signed certificate, run:
    # openssl genrsa -out client.key 2048
    # openssl req -new -key client.key -out client.crt.req
    # openssl x509 -req -days 365 -in client.crt.req -signkey client.key -out client.crt
    # cp client.crt ca.crt
    #
    security = {
        # plaintext, sasl_plaintext, ssl, sasl_ssl
        protocol = "sasl_ssl";
    };
    ssl = {
        ca = {
            location = "ca.crt";
        };
        # Can be omitted if client verification is not performed
        certificate = {
            location = "client.crt";
        };
        # Can be omitted if client verification is not performed
        key = {
            location = "client.key";
            password = "";
        };
    };
    # Can be omitted if SASL authorization is not performed
    sasl = {
        username = "test";
        password = "123";
    };
    # debug options
    debug = "all";
};

...
```

## Docker

### Build

```
docker build -t kafka-repeater --build-arg APT_MIRROR_URL=http://mirrors.aliyun.com/ubuntu -f Dockerfile ../..
```

### Run

#### Forward only

```
docker run -it --restart always -d \
    --name kafka-repeater \
    -e kafka_in_brokers="local-kafka:9092" \
    -e kafka_in_topic="testin" \
    -e kafka_in_group_id="testgrp" \
    -e kafka_out_brokers="remote-kafka:9092" \
    -e kafka_out_topic="testout" \
    kafka-repeater
```


#### Forward to a kafka cluster that requiring SSL authentication

```
docker run -it --restart always -d \
    --name kafka-repeater \
    -e kafka_in_brokers="local-kafka:9092" \
    -e kafka_in_topic="testin" \
    -e kafka_in_group_id="testgrp" \
    -e kafka_out_brokers="remote-kafka:9092" \
    -e kafka_out_topic="testout" \
    -e kafka_out_use_ssl="true" \
    -e kafka_ssl_ca_content="<cert-text>" \
    -e kafka_ssl_certificate_content="<cert-text>" \
    -e kafka_ssl_key_content="<cert-text>" \
    -e kafka_ssl_key_password="" \
    kafka-repeater
```

`<cert-text>`: Open your certificate and paste the text it contains here. You may need to escape the newline as `\n`.

#### Forward to a kafka cluster that requiring SSL+SASL authentication

```
docker run -it --restart always -d \
    --name kafka-repeater \
    -e kafka_in_brokers="local-kafka:9092" \
    -e kafka_in_topic="testin" \
    -e kafka_in_group_id="testgrp" \
    -e kafka_out_brokers="remote-kafka:9092" \
    -e kafka_out_topic="testout" \
    -e kafka_out_use_ssl="true" \
    -e kafka_ssl_ca_content="<cert-text>" \
    -e kafka_ssl_certificate_content="<cert-text>" \
    -e kafka_ssl_key_content="<cert-text>" \
    -e kafka_sasl_username="test" \
    -e kafka_sasl_password="123" \
    kafka-repeater
```

`<cert-text>`: Open your certificate and paste the text it contains here. You may need to escape the newline as `\n`.


#### Full parameters

```
docker run -it --restart always -d \
    --name kafka-repeater \
    -e log_repeated_number_display_interval="10" \
    -e kafka_in_brokers="local-kafka:9092" \
    -e kafka_in_topic="testin" \
    -e kafka_in_group_id="testgrp" \
    -e kafka_out_brokers="remote-kafka:9092" \
    -e kafka_out_topic="testout" \
    -e kafka_out_use_ssl="false" \
    -e kafka_ssl_ca_content="<cert-text>" \
    -e kafka_ssl_certificate_content="<cert-text>" \
    -e kafka_ssl_key_content="<cert-text>" \
    -e kafka_ssl_key_password="" \
    -e kafka_sasl_username="test" \
    -e kafka_sasl_password="123" \
    -e message_convertor="<convertor>" \
    -e share_diff_changer_job_brokers="local-kafka:9092" \
    -e share_diff_changer_job_topic="testjob" \
    -e share_diff_changer_job_group_id="testgrp" \
    -e share_diff_changer_job_time_offset="30" \
    kafka-repeater
```

`<convertor>`: One of the following:
* share_diff_changer_bitcoin_v1
* share_diff_changer_bitcoin_v2_to_v1
* share_printer_bitcoin_v1
* message_printer_print_hex
* Any other value or empty (means forwarding only)
