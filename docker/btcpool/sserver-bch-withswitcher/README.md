Docker for BCHPool's sserver
============================

* Docker Image OS: `Ubuntu 18.04 LTS`
* BCHPool Branch: `master`
* Linked Chain: `BCH` `(bitcoin abc 0.17.1)`
* CMake Options: `POOL__WORK_WITH_STRATUM_SWITCHER=ON`

## Install Docker

```
# Use official mirrors
curl -fsSL https://get.docker.com | bash -s docker

# Or use Aliyun mirrors
curl -fsSL https://get.docker.com | bash -s docker --mirror Aliyun
```

## Build Docker Images

```
cd /work

git clone https://github.com/btccom/btcpool.git
cd btcpool/docker/btcpool/sserver-bch-withswitcher

# build
docker build -t sserver-bch-withswitcher --build-arg JOBS=4 .
# docker build --no-cache -t sserver-bch-withswitcher --build-arg JOBS=4 .
```

## Export and Import the Image
```
# export
docker save sserver-bch-withswitcher | gzip > sserver-bch-withswitcher.img.gz

# import
docker load -i sserver-bch-withswitcher.img.gz
```

## Create Runtime Dirs and Config file

```
# mkdir for sserver
mkdir -p /work/btcpool.bcc/build/run_sserver/log_sserver

# sserver.cfg
vim /work/btcpool.bcc/build/run_sserver/sserver.cfg
```

If you have an old sserver.cfg, you may want this:
```
sed -i s@/btcpool.bcc/@/btcpool/@g /work/btcpool.bcc/build/run_sserver/sserver.cfg
```

You are free to choose the path of your config/log dir.
The above path is only for compatibility with previous deployments.

### sserver.cfg example

```
testnet = false;

kafka = {
  brokers = "x.x.x.x:9092";
};

sserver = {
  ip = "0.0.0.0";
  port = 3333;

  # should be global unique, range: [1, 255]
  id = 1;

  # write last mining notify job send time to file, for monitor
  file_last_notify_time = "/work/btcpool/build/run_sserver/sserver_lastnotifytime.txt";

  # how many seconds between two share submit
  share_avg_seconds = 25;

  ########################## dev options #########################

  # if enable simulator, all share will be accepted. for testing
  enable_simulator = false;

  # if enable it, all share will make block and submit. for testing
  enable_submit_invalid_block = false;

  # if enable, difficulty sent to miners is always miner_difficulty. for development
  enable_dev_mode = false;

  # difficulty to send to miners. for development
  miner_difficulty = 0.005;

  ###################### end of dev options ######################

  // version_mask, uint32_t
  //          2(0x00000002) : allow client change bit 1
  //         16(0x00000010) : allow client change bit 4
  //  536862720(0x1fffe000) : allow client change bit 13 to 28
  //
  //  version_mask = 0;
  //  version_mask = 16;
  //  version_mask = 536862720; // recommended, BIP9 security
  //  ...
  //
  version_mask = 536862720;
};

users = {
  #
  # https://example.com/get_user_id_list?last_id=0
  # {"err_no":0,"err_msg":null,"data":{"jack":1,"terry":2}}
  #
  # There is a demo: https://github.com/btccom/btcpool/issues/16#issuecomment-278245381
  #
  list_id_api_url = "https://example.com/get_user_id_list";
};
```

## Start Docker Container

```
# start docker
docker run -it -v /work/btcpool.bcc/build/run_sserver:/work/btcpool/build/run_sserver --name bch-sserver --network host --restart always -d sserver-bch-withswitcher

# login
docker exec -it bch-sserver /bin/bash
```
