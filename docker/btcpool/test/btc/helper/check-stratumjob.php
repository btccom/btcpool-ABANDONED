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
	"jobId": 6680792843718568961,
	"gbtHash": "98da473b3b03010f209b7fc8c85c7787c6412aaafc0032a586fb8a0eebd12c43",
	"prevHash": "5d11640011603cac4d4ad9bdf8dbb7cd15c9db9d4ec6b87f0bd369a590c3cee5",
	"prevHashBeStr": "90c3cee50bd369a54ec6b87f15c9db9df8dbb7cd4d4ad9bd11603cac5d116400",
	"height": 6,
	"coinbase1": "02000000010000000000000000000000000000000000000000000000000000000000000000ffffffff2a5604b1f1b65c726567696f6e312f50726f6a65637420425443506f6f6c2f",
	"coinbase2": "ffffffff0200f2052a010000001976a914c0174e89bd93eacd1d5a1af4ba1802d412afc08688ac0000000000000000266a24aa21a9ede2f61c3f71d1defd3fa999dfa36953755c690689799962b48bebd836974e8cf900000000",
	"merkleBranch": "",
	"nVersion": 536870912,
	"nBits": 545259519,
	"nTime": 1555489221,
	"minTime": 1555489211,
	"coinbaseValue": 5000000000,
	"witnessCommitment": "6a24aa21a9ede2f61c3f71d1defd3fa999dfa36953755c690689799962b48bebd836974e8cf9",
	"nmcBlockHash": "0000000000000000000000000000000000000000000000000000000000000000",
	"nmcBits": 0,
	"nmcHeight": 0,
	"nmcRpcAddr": "",
	"nmcRpcUserpass": "",
	"rskBlockHashForMergedMining": "",
	"rskNetworkTarget": "0x0000000000000000000000000000000000000000000000000000000000000000",
	"rskFeesForMiner": "",
	"rskdRpcAddress": "",
	"rskdRpcUserPwd": "",
	"isRskCleanJob": false,
	"mergedMiningClean": false
}
EOF
);

$heights = [];
foreach ($lines as $line) {
    $obj = json_decode($line);
    if (FALSE === $obj) {
        exitln("Wrong JSON: $line");
    }

    try {
        checkObject($obj, $rightObj);
    } catch (Exception $ex) {
        exitln($ex->getMessage()."\njob: $line");
    }

    $heights[] = $obj->height;
}

$count = count($heights);
if ($count < 10) {
    exitln("Too few jobs: only $count, must >= 10");
}

$base = $heights[0];
for ($i=1; $i<$count; $i++) {
	$current = $heights[$i];
	if ($current < $base) {
		break;
	}
	$base = $current;
}

if ($i < 15) {
	exitln("Job height cannot increase\nheights: ".implode(', ', $heights));
}
if ($i >= $count - 3) {
	exitln("Job height cannot decrease\nheights: ".implode(', ', $heights));
}

$jBase = $heights[0];
$jSame = true;
for ($j=1; $j<$i; $j++) {
	if ($heights[$j] != $jBase) {
		$jSame = false;
		break;
	}
}
if ($jSame) {
	exitln("Job height cannot increase\nheights: ".implode(', ', $heights));
}

$base = $heights[++$i];
for (; $i<$count; $i++) {
	$current = $heights[$i];
	if ($current < $base) {
		break;
	}
	$base = $current;
}
if ($i != $count) {
	exitln("Job height cannot increase after decreasing\nheights: ".implode(', ', $heights));
}

exitln("All StratumJobs are OK.\nJob heights: ".implode(', ', $heights), 0);
