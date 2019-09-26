#!/usr/bin/env php
<?php
require_once __DIR__.'/functions.php';
set_error_handler('exitOnWarning');

if ($argc < 2) {
    exitln("Usage:\n$argc[0] <json-lines-file>");
}

$path = $argv[1];
$lines = file($path);

$rightObj = json_decode(
<<<'EOF'
{
	"id": null,
	"method": "mining.notify",
	"params": [
		"42",
		"cfd542c2e46ab61bc29ca0c2ebab4e632e3bd0772982797aabed53a764e9f720",
		"02000000010000000000000000000000000000000000000000000000000000000000000000ffffffff2a59040cf4b65c726567696f6e312f50726f6a65637420425443506f6f6c2f",
		"ffffffff0200f2052a010000001976a914c0174e89bd93eacd1d5a1af4ba1802d412afc08688ac0000000000000000266a24aa21a9ede2f61c3f71d1defd3fa999dfa36953755c690689799962b48bebd836974e8cf900000000",
		[],
		"20000000",
		"207fffff",
		"5cb6de08",
		true
	]
}
EOF
);

$heights = [];
foreach ($lines as $line) {
    $obj = json_decode($line);
    if (FALSE === $obj) {
        exitln("Wrong JSON: $line");
	}
	
	if (!isset($obj->method) || $obj->method !== 'mining.notify') {
		continue;
	}

    try {
        checkObject($obj, $rightObj);
    } catch (Exception $ex) {
        exitln($ex->getMessage()."\njob");
    }
}

exitln("All jobs are OK.", 0);
