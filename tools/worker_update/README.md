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
