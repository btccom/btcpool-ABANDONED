<?php
# A demo for `users.list_id_api_url` in `sserver.cfg` with static user list.

header('Content-Type: application/json');

$last_id = (int) $_GET['last_id'];

$users = [
    'aaa' => 1,
    'bbb' => 2,
    'user3' => 3,
    'mmm' => 4,
    'user5' => 5,
    'ddd' => 6,
    'hu60' => 7,
	'taaa' => 31,
    'tbbb' => 32,
    'tuser3' => 33,
    'tmmm' => 34,
    'tuser5' => 35,
    'tddd' => 36,
    'thu60' => 37,
    'daad_ltc' => 41,
    'DdDdDdDdD' => 42,
    'userListAPITest_bch' => 43,
];

$requestedUsers = [];
foreach ($users as $name=>$id) {
    if ($id > $last_id) {
        $requestedUsers [$name] = $id;
    }
}

echo json_encode(
    [
        'err_no' => 0,
        'err_msg' => null,
        'data' => (object) $requestedUsers,
    ]
);
