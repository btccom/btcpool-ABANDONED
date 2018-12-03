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
share_convertor -i /work/sharelog/sharelog-2018-08-21.bin -o sharelog-2018-08-21.parquet -n 1000000
```

Params:
* `-i` Input sharelog file
* `-o` Output parquet file
* `-n` Number of row in each parquet row group
