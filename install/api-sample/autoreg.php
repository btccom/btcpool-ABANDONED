<?php
# A demo for `UserAutoRegAPI.URL` in `btcpool-go-modules/initUserCoin/config.json`.
# Registered users will be saved in `userlist.json`.

error_reporting(E_ALL & ~E_NOTICE & ~E_WARNING);
header('Content-Type: application/json');

$data = json_decode(file_get_contents('php://input'), true);
$name = strtolower(trim($data['sub_name'] ?? ''));
if (empty($name)) {
    echo json_encode(
        [
            'status' => 'failed',
            'message' => 'sub_name cannot be empty',
            'data' => null
        ]
    );
    return;
}

$file = __DIR__.'/userlist.json';
$users = json_decode(file_get_contents($file), true) ?? [];

if (isset($users[$name])) {
    $puid = $users[$name];
}
else {
    $puid = max(array_values($users)) + 1;

    $users[$name] = $puid;
    file_put_contents($file, json_encode($users));
}

echo json_encode(
    [
        'status' => 'success',
        'message' => 'success',
        'data' => [
            'puid' => $puid
        ]
    ]
);
