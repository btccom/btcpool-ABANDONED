Convert share binlog to apache parquet file
==================

### install apache arrow

Reference: https://arrow.apache.org/install/

Install from a software source:
```
apt install -y apt-transport-https lsb-release
LSB_RELEASE=`lsb_release --id --short | tr 'A-Z' 'a-z'`
LSB_RELEASE_SHORT=`lsb_release --codename --short`
curl https://packages.red-data-tools.org/$LSB_RELEASE/red-data-tools-keyring.gpg | apt-key add -
tee /etc/apt/sources.list.d/red-data-tools.list <<APT_LINE
deb https://packages.red-data-tools.org/$LSB_RELEASE/ $LSB_RELEASE_SHORT universe
deb-src https://packages.red-data-tools.org/$LSB_RELEASE/ $LSB_RELEASE_SHORT universe
APT_LINE
apt update
apt install -y libarrow-dev libarrow-glib-dev libparquet-dev libparquet-glib-dev
```

Or build by yourself:
```
apt update
apt install -y build-essential autoconf automake cmake libtool bison flex pkg-config libboost-all-dev libssl-dev
wget http://archive.apache.org/dist/arrow/arrow-0.11.0/apache-arrow-0.11.0.tar.gz
tar zxf apache-arrow-0.11.0.tar.gz
cd apache-arrow-0.11.0/cpp
mkdir release
cd release
cmake -DARROW_PARQUET=ON -DCMAKE_BUILD_TYPE=Release ..
make -j
make install
```

### build

```bash
git clone https://github.com/btccom/btcpool.git
cd btcpool/tools/sharelog_to_parquet
mkdir build
cd build
cmake ..
make
```

### run

```bash
# Generate parquet files based on today and later's sharelog
sharelog_to_parquet -c sharelog_to_parquet.cfg

# Generate parquet files based on an early sharelog
sharelog_to_parquet -c sharelog_to_parquet.cfg -d 20190916
```


## Docker

### Build

```
docker build -t sharelog-to-parquet --build-arg APT_MIRROR_URL=http://mirrors.aliyun.com/ubuntu -f Dockerfile ../..
```

### Run

```
# Generate parquet files based on today and later's sharelog
docker run -it --restart always -d \
    --name sharelog-to-parquet \
    -v "/work/sharelog:/work/sharelog" \
    -v "/work/parquet:/work/parquet" \
    -e sharelog_chain_type="BTC" \
    -e sharelog_data_dir="/work/sharelog" \
    -e parquet_data_dir="/work/parquet" \
    sharelog-to-parquet

# Generate parquet files based on an early sharelog
docker run -it --rm \
    -v "/work/sharelog:/work/sharelog" \
    -v "/work/parquet:/work/parquet" \
    -e sharelog_chain_type="BTC" \
    -e sharelog_data_dir="/work/sharelog" \
    -e parquet_data_dir="/work/parquet" \
    sharelog-to-parquet -d 20190916
```
