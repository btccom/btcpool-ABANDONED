Worker Update
==================

a tool to update worker's information from kafka topic `CommonEvents` to mysql table `mining_workers`.

### build

```bash
mkdir build
cd build
cmake ..
make
```

### run

```bash
cp ../worker_update.cfg .
vim worker_update.cfg
./worker_update -c worker_update.cfg
./worker_update -c worker_update.cfg -l stderr
mkdir log
./worker_update -c worker_update.cfg -l log
```

### docker build

```
sudo docker build -t btcpool-worker-update --build-arg APT_MIRROR_URL=http://mirrors.aliyun.com/ubuntu -f ./Dockerfile ../..
```

### docker run

```
docker run -it --restart always -d \
    --name worker-update \
    -e kafka_brokers="kafka:9092" \
    -e kafka_common_events_topic="CommonEvents" \
    -e kafka_group_id="worker_update_grp" \
    -e pooldb_host="mysql" \
    -e pooldb_username="root" \
    -e pooldb_password="root" \
    -e pooldb_dbname="bpool_local_db" \
    btcpool-worker-update
```
