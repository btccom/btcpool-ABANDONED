<?php
# A demo for `users.list_id_api_url` in `sserver.cfg` with `POOL__USER_DEFINED_COINBASE=ON`.
# User-defined coinbase will be updated randomly.

header('Content-Type: application/json');

$last_id = (int) $_GET['last_id'];
$last_time = (int) $_GET['last_time'];

$users = [
    'user1' => ['puid'=>1, 'coinbase'=>str_shuffle('hiuser1')],
    'user2' => ['puid'=>2, 'coinbase'=>str_shuffle('hiuser2')],
    'user3' => ['puid'=>3, 'coinbase'=>str_shuffle('hiuser3')],
    'user4' => ['puid'=>4, 'coinbase'=>str_shuffle('hiuser4')],
    'user5' => ['puid'=>5, 'coinbase'=>str_shuffle('hiuser5')],
];

$time = time();
$times = [
    'user1' => rand($time-100, $time+20),
    'user2' => rand($time-100, $time+20),
    'user3' => rand($time-100, $time+20),
    'user4' => rand($time-100, $time+20),
    'user5' => rand($time-100, $time+20),
];

$echoUsers = [];

foreach ($users as $uname=>$value) {
    $t = $times[$uname];

    if ($value['puid'] > $last_id || $t > $last_time) {
        $echoUsers[$uname] = $value;
    }
}

echo json_encode(
    [
        'err_no' => 0,
        'err_msg' => null,
        'data' => [
            'users' => $echoUsers,
            'time' => $time,
        ]
    ]
);

