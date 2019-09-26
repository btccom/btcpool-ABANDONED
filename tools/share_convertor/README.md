Sharelog Convertor
==================

* Convert a Bitcoin sharelog v2 file to sharelog v1, then it can be handled by a legacy slparser.

### build

```bash
mkdir build
cd build
cmake ..
make
```

### run

```bash
./share_convertor -i /work/sharelogv2/sharelogBCH-2018-08-27.bin -o /work/sharelogv1/sharelog-2018-08-27.bin
```
