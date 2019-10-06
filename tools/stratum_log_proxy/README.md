Stratum Log Proxy
==================

* Log Ethereum shares to MySQL with the Proxy
* Only support ETHProxy protocol at current
* TODO: add more protocols and chains

The initial directory of all below commands is the directory where the README.md is located.

### build

```bash
mkdir build
cd build
cmake ..
make
```

### run

```bash
cd build
cp ../stratum_log_proxy.cfg .
stratum_log_proxy -c ./stratum_log_proxy.cfg
```

## Docker

### Build

```
docker build -t stratum-log-proxy --build-arg APT_MIRROR_URL=http://mirrors.aliyun.com/ubuntu -f Dockerfile ../..
```

### Run

```
mkdir cfg
cp ./stratum_log_proxy.cfg cfg/
docker run --rm --network=host \
    -v $PWD/cfg:/cfg \
    -v /var/run/mysqld:/var/run/mysqld \
    stratum-log-proxy -c /cfg/stratum_log_proxy.cfg
```
