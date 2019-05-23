<?php
# A demo for `UserCoinMapURL` in `btcpool-go-modules/switcherAPIServer/config.json`.
# The coin of users mining will be updated randomly.

header('Content-Type: application/json');

$last_id = (int) $_GET['last_id'];

$coins = ["btc", "bcc"];

$users = [
    'hu60' => $coins[rand(0,1)],
    'YihaoTest' => $coins[rand(0,1)],
    'YihaoTest3' => $coins[rand(0,1)],
    'testpool' => $coins[rand(0,1)],
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
