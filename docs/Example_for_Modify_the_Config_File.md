# Example : Modify the Config File to Deploy Pool Function
if you want to run all pool function in one machine, the follow config maybe be useful.

### start sserver service：
1. vim  /work/btcpool/build/run_gbtmaker/gbtmaker.cfg
```conf
# config your params for bitcoind And kafka
# example : 
bitcoind = {
    zmq_addr = "tcp://127.0.0.1:18331"
    rpc_addr    = "http://127.0.0.1:18554";  // http://127.0.0.1:8332
    rpc_userpwd = "rpc:V6ASfUg6RXhX5K16eJ6ZLCiws7mc0S"
}
kafka = {
    brokers = "127.0.0.1:9092"
}
```

2. vim  /work/btcpool/build/run_blkmaker/blkmaker.cfg
```conf
# config your params for bitcoinds, kafka, pooldb;
# example :
bitcoinds = (
{
    rpc_addr    = "http://127.0.0.1:18554";  // http://127.0.0.1:8332
    rpc_userpwd = "rpc:V6ASfUg6RXhX5K16eJ6ZLCiws7mc0S";  // username:password
}
)
kafka = {
    brokers = "127.0.0.1:9092";
}
pooldb = {
  host = "localhost";
  port = 3306;
  username = "root";
  password = "yyx2898887384";
  dbname = "bpool_local_db";
};
```

3. vim  /work/btcpool/build/run_blkmaker/statshttpd.cfg
```conf
# config your params for kafka, statahttpd, pooldb
kafka = {
  brokers = "127.0.0.1:9092";
};
statshttpd = {
  ip = "127.0.0.1";
  port = 8080;
  flush_db_interval = 15;
  file_last_flush_time = "/work/btcpool/build/run_statshttpd/statshttpd_lastflushtime.txt";
};
pooldb = {
  host = "localhost";
  port = 3306;
  username = "root";
  password = "yyx2898887384";
  dbname = "bpool_local_db";
};
```

4. vim /work/btcpool/build/run_jobmaker/jobmaker.cfg
```conf
# config your params for testnet, jobmaker, kafka, zookeeper And pool;
# default is using testnet3
testnet = true;
jobmaker = {
    stratum_job_interval = 20;
    gbt_life_time = 90;
    empty_gbt_life_time = 15;
    file_last_job_time = "/work/btcpool/build/run_jobmaker/jobmaker_lastjobtime.txt";
    block_version = 536870914;
};
kafka = {
  brokers = "127.0.0.1:9092";
};
zookeeper = {
  brokers = "127.0.0.1:2181";
};
```

5. vim /work/btcpool/build/run_sserver/sserver.cfg
```conf
# config your params for testnet, kafka, sserver And users;
# default is using testnet3
testnet = true;
kafka = {
  brokers = "127.0.0.1:9092";
};
sserver = {
  ip = "0.0.0.0";
  port = 3333;
  id = 1;
  file_last_notify_time = "/work/btcpool/build/run_sserver/sserver_lastnotifytime.txt";
   enable_simulator = false;
   enable_submit_invalid_block = false;
   share_avg_seconds = 10;
};
users = {
    :list_id_api_url = "http://localhost:8000/userlist.php";
};
```

### start php service
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
`mkdir -p /work/script/web`
edit conf
`vim /work/script/web/userlist.php`
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


    


