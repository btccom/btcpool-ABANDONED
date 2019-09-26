# Example : Modify the Config File to Deploy Pool Function
if you want to run all pool function in one machine, the follow config maybe be useful.

### BTCPool config files：
1. vim /work/btcpool/build/run_gbtmaker/gbtmaker.cfg
```conf
gbtmaker = {
  // rpc call interval seconds
  rpcinterval = 5;

  // check zmq when startup
  is_check_zmq = false;
};

bitcoind = {
  // bitcoind MUST with zmq options: -zmqpubhashblock, -zmqpubhashtx
  // '-zmqpubhashtx' will use to check zmq is working when startup gbtmaker
  zmq_addr = "tcp://127.0.0.1:18333";

  // rpc settings
  rpc_addr    = "http://127.0.0.1:18332";  // http://127.0.0.1:8332
  rpc_userpwd = "test:123";  // username:password
};

kafka = {
  brokers = "127.0.0.1:9092";
};
```

2. vim /work/btcpool/build/run_blkmaker/blkmaker.cfg
```conf
// submit block hex
bitcoinds = (
{
  rpc_addr    = "http://127.0.0.1:18332";  // http://127.0.0.1:8332
  rpc_userpwd = "test:123";  // username:password
}
//,
//{
//rpc_addr    = "";  // http://127.0.0.1:8332
//rpc_userpwd = "";  // username:password
//}
);

kafka = {
  brokers = "127.0.0.1:9092";
};

#
# pool mysql db: table.found_blocks
#
pooldb = {
  host = "localhost";
  port = 3306;
  username = "root";
  password = "root";
  dbname = "bpool_local_db";
};
```

3. vim /work/btcpool/build/run_jobmaker/jobmaker.cfg
```conf
# is using testnet3
testnet = true;

jobmaker = {
  # send stratum job interval seconds
  stratum_job_interval = 20;

  # max gbt life cycle time seconds
  gbt_life_time = 90;

  # max empty gbt life cycle time seconds
  # CAUTION: the value SHOULD >= 10. If non-empty job not come in 10 seconds,
  #          jobmaker will always make a previous height job until its arrival
  empty_gbt_life_time = 15;

  # write last stratum job send time to file
  file_last_job_time = "/work/btcpool/build/run_jobmaker/jobmaker_lastjobtime.txt";

  # block version, default is 0 means use the version which returned by bitcoind
  # or you can specify the version you want to signal.
  # more info: https://github.com/bitcoin/bips/blob/master/bip-0009.mediawiki
  # Example: 0, 536870914(0x20000002), ..., etc.
  block_version = 536870914;
};

kafka = {
  brokers = "127.0.0.1:9092";
};

zookeeper = {
  brokers = "127.0.0.1:2181";
};

pool = {
  # payout address
  # the private key of my2dxGb5jz43ktwGxg2doUaEb9WhZ9PQ7K is cQAiutBRMq4wwC9JHeANQLttogZ2EXw9AgnGXMq5S3SAMmbX2oLd
  payout_address = "my2dxGb5jz43ktwGxg2doUaEb9WhZ9PQ7K";
  # coinbase info with location ID (https://github.com/btccom/btcpool/issues/36)
  coinbase_info = "1/test/";
};
```

4. vim /work/btcpool/build/run_sserver/sserver.cfg
```conf
# is using testnet3
testnet = true;

kafka = {
  brokers = "127.0.0.1:9092";
};

sserver = {
  ip = "0.0.0.0";
  port = 3333;

  // should be global unique, range: [1, 255]
  id = 1;

  // write last mining notify job send time to file, for monitor
  file_last_notify_time = "/work/btcpool/build/run_sserver/sserver_lastnotifytime.txt";

  // if enable simulator, all share will be accepted. for testing
  enable_simulator = true;

  // if enable it, all share will make block and submit. for testing
  enable_submit_invalid_block = false;

  // how many seconds between two share submit
  share_avg_seconds = 10;
};

users = {
  //
  // https://example.com/get_user_id_list?last_id=0
  // {"err_no":0,"err_msg":null,"data":{"jack":1,"terry":2}}
  //
  list_id_api_url = "http://localhost:8000/userlist.php";
};
```

5. vim /work/btcpool/build/run_statshttpd/statshttpd.cfg
```conf
kafka = {
  brokers = "127.0.0.1:9092";
};

statshttpd = {
  ip = "0.0.0.0";
  port = 8080;

  # interval seconds, flush workers data into database
  # it's very fast because we use insert statement with multiple values and
  # merge table when flush data to DB. we have test mysql, it could flush
  # 25,000 workers into DB in about 1.7 seconds.
  flush_db_interval = 15;
  # write last db flush time to file
  file_last_flush_time = "/work/btcpool/build/run_statshttpd/statshttpd_lastflushtime.txt";
};


#
# pool mysql db: table.mining_workers
#
pooldb = {
  host = "localhost";
  port = 3306;
  username = "root";
  password = "root";
  dbname = "bpool_local_db";
};
```

6. vim /work/btcpool/build/run_sharelogger/sharelogger.cfg
```conf
kafka = {
  brokers = "127.0.0.1:9092";
};

sharelog_writer = {
  // data dir for share bin log
  data_dir = "/work/btcpool/data/sharelog";

  // kafka group id (ShareLog writer use Kafka High Level Consumer)
  // use different group id for different servers. once you have set it,
  // do not change it unless you well know about Kafka.
  kafka_group_id = "sharelog_write_1";
};
```

7. vim /work/btcpool/build/run_slparser/slparser.cfg
```conf
slparserhttpd = {
  ip = "127.0.0.1";
  port = 8081;

  # interval seconds, flush stats data into database
  # it's very fast because we use insert statement with multiple values and
  # merge table when flush data to DB. we have test mysql, it could flush
  # 50,000 itmes into DB in about 2.5 seconds.
  flush_db_interval = 15;
};

sharelog = {
  data_dir = "/work/btcpool/data/sharelog";
};

#
# pool mysql db: table.stats_xxxx
#
pooldb = {
  host = "localhost";
  port = 3306;
  username = "root";
  password = "root";
  dbname = "bpool_local_stats_db";
};
```

### User List Web API
vim /etc/supervisor/conf.d/php-web.conf
```conf
[program:php-web]
directory=/work/script/web
command=/usr/bin/php -S localhost:8000 -t /work/script/web/
autostart=true
autorestart=true
startsecs=1
startretries=20

redirect_stderr=true
stdout_logfile_backups=5
stdout_logfile=/work/script/php-web.log
```
create php script 
```
mkdir -p /work/script/web
```
edit conf
```
vim /work/script/web/userlist.php
```
```php
<?php
header('Content-Type: application/json');
$last_id = (int) $_GET['last_id'];
$users = [
    'user1' => 1,
    'user2' => 2,
    'user3' => 3,
    'user4' => 4,
    'user5' => 5,
];
if ($last_id >= count($users)) {
    $users = [];
}
echo json_encode(
    [
        'err_no' => 0,
        'err_msg' => null,
        'data' => (object) $users,
    ]
);
```
